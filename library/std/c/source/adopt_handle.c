#include "r_std_c.h"

RStdCHandle r_std_c_adopt_handle(void *pointer, RStdCHandleDestructor destructor) {
    return (RStdCHandle){pointer, destructor};
}
