#ifndef RIBALTA_GPU_H
#define RIBALTA_GPU_H

/* Solo dentro Flatpak ($FLATPAK_ID impostata): il runtime porta TUTTI i driver Vulkan di Mesa
 * (radeon, lvp, nouveau, …) e il loader li carica tutti, compresa libLLVM (~35 MB di RAM in più).
 * Legge i vendor PCI delle GPU da /sys/class/drm/card* e imposta VK_LOADER_DRIVERS_SELECT sui soli
 * driver che servono (Intel, AMD, NVIDIA, virtio). Non fa nulla se l'utente ha già scelto i driver
 * (VK_LOADER_DRIVERS_SELECT, VK_DRIVER_FILES, VK_ICD_FILENAMES) o se nessun vendor è riconosciuto.
 * Va chiamata prima di SDL_Init. Se il driver scelto non funziona, SDL ripiega su OpenGL. */
void gpu_limit_vulkan_drivers(void);

#endif
