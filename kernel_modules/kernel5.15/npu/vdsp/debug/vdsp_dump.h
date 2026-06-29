/**
 * SPDX-FileContributor: vdsp driver debug function, dump vdsp data.
 */


#ifndef _VDSP_DUMP_H_
#define _VDSP_DUMP_H_

int32_t xrp_save_file(const char *filename, const char *buffer, uint32_t size);
int32_t xrp_dump_libraries(struct xvp *xvp);
#endif
