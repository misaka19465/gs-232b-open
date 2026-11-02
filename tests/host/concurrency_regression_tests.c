#include "concurrency_test_support.h"

int main(void)
{
    if (!concurrency_upload_tests()) return EXIT_FAILURE;
    if (!concurrency_pressure_tests()) return EXIT_FAILURE;
    if (!concurrency_fault_tests()) return EXIT_FAILURE;
    puts("Four WebSockets plus UART: bounded upload, pressure, epochs and faults passed");
    return EXIT_SUCCESS;
}
