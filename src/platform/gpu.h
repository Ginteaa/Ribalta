#ifndef RIBALTA_GPU_H
#define RIBALTA_GPU_H

/* Solo Linux: limita i driver Vulkan che il loader carica (VK_LOADER_DRIVERS_SELECT), leggendo i
 * vendor PCI delle GPU da /sys/class/drm/card* e quale GPU gestisce lo schermo (boot_vga = 1).
 *  - PC con GPU di produttori diversi (portatili ibridi Intel + NVIDIA, …), nativo o Flatpak: solo i
 *    driver della GPU con boot_vga, cioè l'integrata. Senza filtro il loader carica anche il driver
 *    NVIDIA (~60 MB di RAM in più) e SDL disegna sulla scheda dedicata, tenendola sveglia.
 *  - Flatpak, negli altri casi: i driver dei vendor presenti (Intel, AMD, NVIDIA, virtio), perché il
 *    runtime porta TUTTI i driver di Mesa e il loader li caricherebbe tutti, libLLVM compresa (~35 MB).
 *  - Nativo con un solo produttore: nessun filtro.
 * Non fa nulla se l'utente ha già scelto i driver o la GPU (VK_LOADER_DRIVERS_SELECT, VK_DRIVER_FILES,
 * VK_ICD_FILENAMES, DRI_PRIME, __NV_PRIME_RENDER_OFFLOAD: es. prime-run) o se nessun vendor è
 * riconosciuto. Va chiamata prima di SDL_Init. Se il driver scelto non funziona, SDL ripiega su OpenGL. */
void gpu_limit_vulkan_drivers(void);

#endif
