/* Scelta dei driver Vulkan su Linux (vedi gpu.h). */
#include "platform/gpu.h"

#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#ifdef __linux__
static bool env_set(const char *name)
{
    const char *v = getenv(name);
    return v && v[0];
}

enum { V_INTEL = 1, V_AMD = 2, V_NVIDIA = 4, V_VIRTIO = 8 };

static unsigned vendor_bit(unsigned pci)
{
    switch (pci) {
    case 0x8086: return V_INTEL;
    case 0x1002: return V_AMD;
    case 0x10de: return V_NVIDIA;
    case 0x1af4: return V_VIRTIO;
    default:     return 0;
    }
}

/* Legge un numero esadecimale (vendor) o decimale (boot_vga) da un file di sysfs */
static bool read_uint(const char *path, const char *fmt, unsigned *out)
{
    FILE *f = fopen(path, "r");
    if (!f)
        return false;
    bool ok = fscanf(f, fmt, out) == 1;
    fclose(f);
    return ok;
}
#endif

void gpu_limit_vulkan_drivers(void)
{
#ifdef __linux__
    if (env_set("VK_LOADER_DRIVERS_SELECT") || env_set("VK_DRIVER_FILES") || env_set("VK_ICD_FILENAMES")
        || env_set("DRI_PRIME") || env_set("__NV_PRIME_RENDER_OFFLOAD"))
        return; /* l'utente ha già scelto driver o GPU */

    unsigned present = 0, boot = 0;
    for (int i = 0; i < 16; i++) {
        char path[64];
        unsigned pci = 0, bv = 0;
        snprintf(path, sizeof path, "/sys/class/drm/card%d/device/vendor", i);
        if (!read_uint(path, "%x", &pci))
            continue;
        present |= vendor_bit(pci);
        snprintf(path, sizeof path, "/sys/class/drm/card%d/device/boot_vga", i);
        if (read_uint(path, "%u", &bv) && bv == 1)
            boot = vendor_bit(pci);
    }

    /* più produttori (portatile ibrido): solo la GPU che gestisce lo schermo, cioè l'integrata;
     * altrimenti il filtro serve solo in Flatpak (il runtime porta tutti i driver di Mesa) */
    bool hybrid = boot && (present & (present - 1)) != 0;
    unsigned want = hybrid ? boot : env_set("FLATPAK_ID") ? present : 0;
    if (!want)
        return; /* nativo con un solo produttore, o GPU sconosciuta: nessun filtro */

    /* glob sui nomi dei file .json dei driver (intel_icd, radeon_icd, nvidia_icd, nouveau_icd, …) */
    char sel[96] = "";
    if (want & V_INTEL)  strcat(sel, "*intel*,");
    if (want & V_AMD)    strcat(sel, "*radeon*,*amd*,");  /* RADV o AMDVLK */
    if (want & V_NVIDIA) strcat(sel, "*nvidia*,*nouveau*,");
    if (want & V_VIRTIO) strcat(sel, "*virtio*,");
    sel[strlen(sel) - 1] = '\0';
    setenv("VK_LOADER_DRIVERS_SELECT", sel, 0);
    if (getenv("RIBALTA_DEBUG"))
        fprintf(stderr, "ribalta: VK_LOADER_DRIVERS_SELECT=%s\n", sel);
#endif /* __linux__: altrove (macOS, Windows) non c'è nulla da filtrare */
}
