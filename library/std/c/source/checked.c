#include "r_std_c.h"

RStdCCheckedResult r_std_c_checked(RStdConvertNumericValue source,
                                   RStdConvertDestination destination) {
    return r_std_convert_checked(source, destination);
}
