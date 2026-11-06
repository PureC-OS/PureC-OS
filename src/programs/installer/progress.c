#include "progress.h"
#include "../../libfs/include/purefs.h"

const char *installer_progress_error_text(int32_t status){
    switch(status){
        case PF_ERROR_IO: return "disk read/write failed";
        case PF_ERROR_NOT_FOUND: return "required file or target not found";
        case PF_ERROR_INVALID: return "invalid disk state";
        case PF_ERROR_NO_SPACE: return "not enough space";
        case PF_ERROR_EXISTS: return "file already exists";
        case PF_ERROR_NOT_FILE: return "expected a file, found a directory";
        case PF_ERROR_NOT_DIR: return "target file system is damaged";
        case PF_ERROR_UNSUPPORTED: return "unsupported disk geometry";
        case PF_ERROR_BUSY: return "storage is busy";
        case PF_ERROR_READ_ONLY: return "target is read-only";
        case PF_ERROR_CONFIRMATION: return "disk identity changed";
        case PF_ERROR_NOT_BLANK: return "disk is not blank";
        case PF_ERROR_TOO_SMALL: return "target disk is too small";
        default: return "unknown storage error";
    }
}
