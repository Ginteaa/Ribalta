/* Scelta dei driver Vulkan dentro Flatpak (vedi gpu.h). */
#include "platform/gpu.h"

#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static bool env_set(const char *name)
{
    const char *v = getenv(name);
    return v && v[0];
}

void gpu_limit_vulkan_drivers(void)
{
    if (!env_set("FLATPAK_ID") || env_set("VK_LOADER_DRIVERS_SELECT") || env_set("VK_DRIVER_FILES")
        || env_set("VK_ICD_FILENAMES"))
        return;

    bool intel = false, amd = false, nvidia = false, virtio = false;
    for (int i = 0; i < 16; i++) {
        char path[64];
        snprintf(path, sizeof path, "/sys/class/drm/card%d/device/vendor", i);
        FILE *f = fopen(path, "r");
        if (!f)
            continue;
        unsigned vendor = 0;
        if (fscanf(f, "%x", &vendor) == 1) {
            intel  |= vendor == 0x8086;
            amd    |= vendor == 0x1002;
            nvidia |= vendor == 0x10de;
            virtio |= vendor == 0x1af4;
        }
        fclose(f);
    }

    /* glob sui nomi dei file .json dei driver (intel_icd, radeon_icd, nvidia_icd, nouveau_icd, …) */
    char sel[96] = "";
    if (intel)  strcat(sel, "*intel*,");
    if (amd)    strcat(sel, "*radeon*,");
    if (nvidia) strcat(sel, "*nvidia*,*nouveau*,");
    if (virtio) strcat(sel, "*virtio*,");
    size_t n = strlen(sel);
    if (n == 0)
        return; /* GPU sconosciuta: nessun filtro */
    sel[n - 1] = '\0';
    setenv("VK_LOADER_DRIVERS_SELECT", sel, 0);
    if (getenv("RIBALTA_DEBUG"))
        fprintf(stderr, "ribalta: VK_LOADER_DRIVERS_SELECT=%s\n", sel);
}
