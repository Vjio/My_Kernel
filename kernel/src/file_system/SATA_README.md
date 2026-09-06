This document will explain how the written SATA driver functions, for future ease of maintaining.

# Structures
The data structures that the SATA needs to function are found in ahci.hpp.

If you wish to read more about each register,
check Intel's documentation https://www.intel.com/content/dam/www/public/us/en/documents/technical-specifications/serial-ata-ahci-spec-rev1-3-1.pdf

# Behaviour
The SATA driver is built to handle NCQ commands (Native Command Queuing) in order to achieve a higher throughput.

NCQ     -> multiple commands can be handled at once (to at most 32)
non-NCQ -> only 1 command can be handled at once (no command queuing)

Currently, for a first version of the driver, the driver can only handle non-NCQ commands. Once the driver is completly written,
NCQ support will be added and non-NCQ behaviour will become a fallback (in case of a very unstable system, the drive will be reset
and non-NCQ commands will be the only type of commands issued).

This means that, in order to truly take advantage of the maximum 32 commands slots per SATA drive,
threads using the SATA driver need to be interrupt driven (going into a WAITING state after issuing a command
and waking up once a completion interrupt fires).

TODO: spawn a kernel thread to handle AHCI controller/SATA driver reading/writings
