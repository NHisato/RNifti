#include <R_ext/Rdynload.h>
#include <Rcpp.h>

#include "niftilib/nifti1_io.h"
#include "lib/NiftiImage.h"

// Defined since R 3.1.0, according to Tomas Kalibera, but there's no reason to break compatibility with 3.0.x
#ifndef MAYBE_SHARED
#define MAYBE_SHARED(x) (NAMED(x) > 1)
#endif

using namespace Rcpp;
using namespace RNifti;

typedef std::vector<float> float_vector;

RcppExport SEXP niftiVersion (SEXP _path)
{
BEGIN_RCPP
    int version = NiftiImage::fileVersion(as<std::string>(_path));
    return wrap(version);
END_RCPP
}

RcppExport SEXP readNifti (SEXP _object, SEXP _internal, SEXP _volumes)
{
BEGIN_RCPP
    if (Rf_isNull(_volumes))
    {
        NiftiImage image(_object);
        return image.toArrayOrPointer(as<bool>(_internal), "NIfTI image");
    }
    else
    {
        std::vector<int> volumes;
        IntegerVector volumesR(_volumes);
        for (int i=0; i<volumesR.length(); i++)
            volumes.push_back(volumesR[i] - 1);
        NiftiImage image(as<std::string>(_object), volumes);
        return image.toArrayOrPointer(as<bool>(_internal), "NIfTI image");
    }
END_RCPP
}

RcppExport SEXP writeNifti (SEXP _image, SEXP _file, SEXP _datatype)
{
BEGIN_RCPP
    NiftiImage image(_image);
    image.toFile(as<std::string>(_file), as<std::string>(_datatype));
    return R_NilValue;
END_RCPP
}

RcppExport SEXP updateNifti (SEXP _image, SEXP _reference, SEXP _datatype)
{
BEGIN_RCPP
    const std::string datatype = as<std::string>(_datatype);
    const bool willChangeDatatype = (datatype != "auto");
    
    if (Rf_isVectorList(_reference) && Rf_length(_reference) < 36)
    {
        NiftiImage image(_image);
        image.update(_reference);
        if (willChangeDatatype)
            image.changeDatatype(datatype);
        return image.toArrayOrPointer(willChangeDatatype, "NIfTI image");
    }
    else
    {
        const NiftiImage reference(_reference);
        if (reference.isNull() && willChangeDatatype)
        {
            NiftiImage image(_image);
            image.changeDatatype(datatype);
            return image.toPointer("NIfTI image");
        }
        else if (reference.isNull() && !willChangeDatatype)
            return _image;
        else
        {
            NiftiImage image = reference;
            image.update(_image);
            if (willChangeDatatype)
                image.changeDatatype(datatype);
            return image.toArrayOrPointer(willChangeDatatype, "NIfTI image");
        }
    }
END_RCPP
}

RcppExport SEXP dumpNifti (SEXP _image)
{
BEGIN_RCPP
    const NiftiImage image(_image, false);
    return image.headerToList();
END_RCPP
}

RcppExport SEXP getXform (SEXP _image, SEXP _preferQuaternion)
{
BEGIN_RCPP
    const NiftiImage image(_image, false);
    const bool preferQuaternion = as<bool>(_preferQuaternion);
    
    ::mat44 xform = image.xform(preferQuaternion);
    NumericMatrix matrix(4,4);
    for (int i=0; i<4; i++)
    {
        for (int j=0; j<4; j++)
            matrix(i,j) = static_cast<double>(xform.m[i][j]);
    }
    
    return matrix;
END_RCPP
}

RcppExport SEXP setXform (SEXP _image, SEXP _matrix, SEXP _isQform)
{
BEGIN_RCPP
    NumericMatrix matrix(_matrix);
    if (matrix.cols() != 4 || matrix.rows() != 4)
        throw std::runtime_error("Specified affine matrix does not have dimensions of 4x4");
    ::mat44 xform;
    for (int i=0; i<4; i++)
    {
        for (int j=0; j<4; j++)
            xform.m[i][j] = static_cast<float>(matrix(i,j));
    }
    
    int code = -1;
    if (!Rf_isNull(matrix.attr("code")))
        code = as<int>(matrix.attr("code"));
    
    if (Rf_isVectorList(_image) && Rf_inherits(_image,"niftiHeader"))
    {
        // Header only
        List image(_image);
        if (MAYBE_SHARED(_image))
            image = image;
        
        float qbcd[3], qxyz[3], dxyz[3], qfac;
        nifti_mat44_to_quatern(xform, &qbcd[0], &qbcd[1], &qbcd[2], &qxyz[0], &qxyz[1], &qxyz[2], &dxyz[0], &dxyz[1], &dxyz[2], &qfac);
        
        if (as<bool>(_isQform))
        {
            *REAL(image["quatern_b"]) = static_cast<double>(qbcd[0]);
            *REAL(image["quatern_c"]) = static_cast<double>(qbcd[1]);
            *REAL(image["quatern_d"]) = static_cast<double>(qbcd[2]);
            *REAL(image["qoffset_x"]) = static_cast<double>(qxyz[0]);
            *REAL(image["qoffset_y"]) = static_cast<double>(qxyz[1]);
            *REAL(image["qoffset_z"]) = static_cast<double>(qxyz[2]);
            REAL(image["pixdim"])[0] = static_cast<double>(qfac);
            if (code >= 0)
                *INTEGER(image["qform_code"]) = code;
        }
        else
        {
            for (int i=0; i<4; i++)
            {
                REAL(image["srow_x"])[i] = matrix(0,i);
                REAL(image["srow_y"])[i] = matrix(1,i);
                REAL(image["srow_z"])[i] = matrix(2,i);
            }
            if (code >= 0)
                *INTEGER(image["sform_code"]) = code;
        }
        
        const int dimensionality = INTEGER(image["dim"])[0];
        for (int i=0; i<std::min(3,dimensionality); i++)
            REAL(image["pixdim"])[i+1] = static_cast<double>(dxyz[i]);
        
        return image;
    }
    else
    {
        // From here, we assume we have a proper image
        // First, duplicate the image object if necessary, to preserve usual R semantics
        NiftiImage image(_image);
        if (MAYBE_SHARED(_image))
            image = image;
    
        if (!image.isNull())
        {
            if (as<bool>(_isQform))
            {
                image->qto_xyz = xform;
                image->qto_ijk = nifti_mat44_inverse(image->qto_xyz);
                nifti_mat44_to_quatern(image->qto_xyz, &image->quatern_b, &image->quatern_c, &image->quatern_d, &image->qoffset_x, &image->qoffset_y, &image->qoffset_z, NULL, NULL, NULL, &image->qfac);
            
                if (code >= 0)
                    image->qform_code = code;
            }
            else
            {
                image->sto_xyz = xform;
                image->sto_ijk = nifti_mat44_inverse(image->sto_xyz);
            
                if (code >= 0)
                    image->sform_code = code;
            }
        }
    
        // If the image was copied above it will have been marked nonpersistent
        if (image.isPersistent())
            return _image;
        else
            return image.toArrayOrPointer(Rf_inherits(_image,"internalImage"), "NIfTI image");
    }
END_RCPP
}

RcppExport SEXP getOrientation (SEXP _image, SEXP _preferQuaternion)
{
BEGIN_RCPP
    const NiftiImage image(_image, false);
    const bool preferQuaternion = as<bool>(_preferQuaternion);
    const std::string orientation = NiftiImage::xformToString(image.xform(preferQuaternion));
    return wrap(orientation);
END_RCPP
}

RcppExport SEXP setOrientation (SEXP _image, SEXP _axes)
{
BEGIN_RCPP
    NiftiImage image(_image);
    if (MAYBE_SHARED(_image))
        image = image;
    image.reorient(as<std::string>(_axes));
    return image.toArrayOrPointer(Rf_inherits(_image,"internalImage"), "NIfTI image");
END_RCPP
}

RcppExport SEXP hasData (SEXP _image)
{
BEGIN_RCPP
    const NiftiImage image(_image);
    return wrap(image->data != NULL);
END_RCPP
}

RcppExport SEXP rescaleImage (SEXP _image, SEXP _scales)
{
BEGIN_RCPP
    const NiftiImage image(_image);
    NiftiImage newImage(image);
    newImage.rescale(as<float_vector>(_scales));
    return newImage.toPointer("NIfTI image");
END_RCPP
}

RcppExport SEXP pointerToArray (SEXP _image)
{
BEGIN_RCPP
    NiftiImage image(_image);
    return image.toArray();
END_RCPP
}

extern "C" {

static R_CallMethodDef callMethods[] = {
    { "niftiVersion",   (DL_FUNC) &niftiVersion,    1 },
    { "readNifti",      (DL_FUNC) &readNifti,       3 },
    { "writeNifti",     (DL_FUNC) &writeNifti,      3 },
    { "updateNifti",    (DL_FUNC) &updateNifti,     3 },
    { "dumpNifti",      (DL_FUNC) &dumpNifti,       1 },
    { "getXform",       (DL_FUNC) &getXform,        2 },
    { "setXform",       (DL_FUNC) &setXform,        3 },
    { "getOrientation", (DL_FUNC) &getOrientation,  2 },
    { "setOrientation", (DL_FUNC) &setOrientation,  2 },
    { "hasData",        (DL_FUNC) &hasData,         1 },
    { "rescaleImage",   (DL_FUNC) &rescaleImage,    2 },
    { "pointerToArray", (DL_FUNC) &pointerToArray,  1 },
    { NULL, NULL, 0 }
};

void R_init_RNifti (DllInfo *info)
{
    R_registerRoutines(info, NULL, callMethods, NULL, NULL);
    R_useDynamicSymbols(info, FALSE);
    
    R_RegisterCCallable("RNifti",   "nii_make_new_header",  (DL_FUNC) &nifti_make_new_header);
    R_RegisterCCallable("RNifti",   "nii_make_new_nim",     (DL_FUNC) &nifti_make_new_nim);
    R_RegisterCCallable("RNifti",   "nii_convert_nhdr2nim", (DL_FUNC) &nifti_convert_nhdr2nim);
    R_RegisterCCallable("RNifti",   "nii_convert_nim2nhdr", (DL_FUNC) &nifti_convert_nim2nhdr);
    R_RegisterCCallable("RNifti",   "nii_copy_nim_info",    (DL_FUNC) &nifti_copy_nim_info);
    R_RegisterCCallable("RNifti",   "nii_copy_extensions",  (DL_FUNC) &nifti_copy_extensions);
    R_RegisterCCallable("RNifti",   "nii_image_unload",     (DL_FUNC) &nifti_image_unload);
    R_RegisterCCallable("RNifti",   "nii_image_free",       (DL_FUNC) &nifti_image_free);
    
    R_RegisterCCallable("RNifti",   "nii_datatype_sizes",   (DL_FUNC) &nifti_datatype_sizes);
    R_RegisterCCallable("RNifti",   "nii_datatype_string",  (DL_FUNC) &nifti_datatype_string);
    R_RegisterCCallable("RNifti",   "nii_units_string",     (DL_FUNC) &nifti_units_string);
    R_RegisterCCallable("RNifti",   "nii_get_volsize",      (DL_FUNC) &nifti_get_volsize);
    R_RegisterCCallable("RNifti",   "nii_update_dims_from_array", (DL_FUNC) &nifti_update_dims_from_array);
    
    R_RegisterCCallable("RNifti",   "nii_set_filenames",    (DL_FUNC) &nifti_set_filenames);
    R_RegisterCCallable("RNifti",   "nii_image_read",       (DL_FUNC) &nifti_image_read);
    R_RegisterCCallable("RNifti",   "nii_image_write",      (DL_FUNC) &nifti_image_write);
    
    R_RegisterCCallable("RNifti",   "nii_mat33_rownorm",    (DL_FUNC) &nifti_mat33_rownorm);
    R_RegisterCCallable("RNifti",   "nii_mat33_colnorm",    (DL_FUNC) &nifti_mat33_colnorm);
    R_RegisterCCallable("RNifti",   "nii_mat33_determ",     (DL_FUNC) &nifti_mat33_determ);
    R_RegisterCCallable("RNifti",   "nii_mat33_inverse",    (DL_FUNC) &nifti_mat33_inverse);
    R_RegisterCCallable("RNifti",   "nii_mat33_mul",        (DL_FUNC) &nifti_mat33_mul);
    R_RegisterCCallable("RNifti",   "nii_mat33_polar",      (DL_FUNC) &nifti_mat33_polar);
    R_RegisterCCallable("RNifti",   "nii_mat44_inverse",    (DL_FUNC) &nifti_mat44_inverse);
    R_RegisterCCallable("RNifti",   "nii_mat44_to_quatern", (DL_FUNC) &nifti_mat44_to_quatern);
    R_RegisterCCallable("RNifti",   "nii_quatern_to_mat44", (DL_FUNC) &nifti_quatern_to_mat44);
    R_RegisterCCallable("RNifti",   "nii_mat44_to_orientation", (DL_FUNC) &nifti_mat44_to_orientation);
    
    R_RegisterCCallable("RNifti",   "nii_swap_2bytes",      (DL_FUNC) &nifti_swap_2bytes);
    R_RegisterCCallable("RNifti",   "nii_swap_4bytes",      (DL_FUNC) &nifti_swap_4bytes);
    R_RegisterCCallable("RNifti",   "nii_swap_8bytes",      (DL_FUNC) &nifti_swap_8bytes);
    R_RegisterCCallable("RNifti",   "nii_swap_16bytes",     (DL_FUNC) &nifti_swap_16bytes);
}

}
