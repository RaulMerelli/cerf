#pragma once

#include "../core/service.h"

enum class ResumeSource { User, Hardware };

/* The single host-side path for surfacing a guest power-state transition to the
   user. Every SoC's power-down / reset detection funnels through here so the
   notification (UART tab + forceful banner, framebuffer re-arm on reboot) is
   uniform across boards - a new SoC adds only its detection, never its own UI. */
class GuestPowerNotifier : public Service {
public:
    using Service::Service;

    /* Guest entered deep sleep / power-off (it will not run again until reset). */
    void NotifyPowerDown();

    /* Guest requested a reset/reboot; also re-arms the framebuffer auto-switch so
       the rebooted guest's video brings the Framebuffer tab back automatically. */
    void NotifyReboot();

    void NotifyResume(ResumeSource src);

    /* Hard reset executed: volatile RAM wiped. Follows the NotifyReboot the
       reset request itself raised, so it only banners. */
    void NotifyHardReset();

private:
    void Banner(const char* line);
};
