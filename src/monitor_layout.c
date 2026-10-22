#include "monitor_layout.h"

#include <X11/extensions/Xrandr.h>

size_t monitor_layout_get(Display *display, Window root,
                          MonitorGeometry *monitors, size_t capacity)
{
    if (display == NULL || monitors == NULL || capacity == 0) {
        return 0;
    }

    int major = 1;
    int minor = 5;
    if (XRRQueryVersion(display, &major, &minor) &&
        (major > 1 || (major == 1 && minor >= 5))) {
        int count = 0;
        XRRMonitorInfo *active = XRRGetMonitors(display, root, True, &count);
        size_t copied = 0;
        for (int i = 0; active != NULL && i < count && copied < capacity; ++i) {
            if (active[i].width <= 0 || active[i].height <= 0) {
                continue;
            }
            monitors[copied++] = (MonitorGeometry){
                active[i].x, active[i].y,
                active[i].width, active[i].height
            };
        }
        RROutput primary = XRRGetOutputPrimary(display, root);
        for (int i = 0; active != NULL && i < count && copied > 0; ++i) {
            for (int output = 0; output < active[i].noutput; ++output) {
                if (active[i].outputs[output] == primary) {
                    MonitorGeometry primary_monitor = {
                        active[i].x, active[i].y,
                        active[i].width, active[i].height
                    };
                    for (size_t j = 0; j < copied; ++j) {
                        if (monitors[j].x == primary_monitor.x &&
                            monitors[j].y == primary_monitor.y &&
                            monitors[j].width == primary_monitor.width &&
                            monitors[j].height == primary_monitor.height) {
                            MonitorGeometry first = monitors[0];
                            monitors[0] = monitors[j];
                            monitors[j] = first;
                            break;
                        }
                    }
                    i = count;
                    break;
                }
            }
        }
        if (active != NULL) {
            XRRFreeMonitors(active);
        }
        if (copied > 0) {
            return copied;
        }
    }

    XRRScreenResources *resources = XRRGetScreenResourcesCurrent(display, root);
    if (resources != NULL) {
        size_t copied = 0;
        for (int i = 0; i < resources->ncrtc && copied < capacity; ++i) {
            XRRCrtcInfo *crtc =
                XRRGetCrtcInfo(display, resources, resources->crtcs[i]);
            if (crtc == NULL) {
                continue;
            }
            if (crtc->mode != None && crtc->width > 0 && crtc->height > 0) {
                int duplicate = 0;
                for (size_t j = 0; j < copied; ++j) {
                    if (monitors[j].x == crtc->x &&
                        monitors[j].y == crtc->y &&
                        monitors[j].width == (int)crtc->width &&
                        monitors[j].height == (int)crtc->height) {
                        duplicate = 1;
                        break;
                    }
                }
                if (!duplicate) {
                    monitors[copied++] = (MonitorGeometry){
                        crtc->x, crtc->y,
                        (int)crtc->width, (int)crtc->height
                    };
                }
            }
            XRRFreeCrtcInfo(crtc);
        }
        RROutput primary = XRRGetOutputPrimary(display, root);
        for (int i = 0; primary != None && i < resources->ncrtc; ++i) {
            XRRCrtcInfo *crtc =
                XRRGetCrtcInfo(display, resources, resources->crtcs[i]);
            if (crtc == NULL) {
                continue;
            }
            int has_primary = 0;
            for (int output = 0; output < crtc->noutput; ++output) {
                if (crtc->outputs[output] == primary) {
                    has_primary = 1;
                    break;
                }
            }
            if (has_primary) {
                for (size_t j = 0; j < copied; ++j) {
                    if (monitors[j].x == crtc->x &&
                        monitors[j].y == crtc->y &&
                        monitors[j].width == (int)crtc->width &&
                        monitors[j].height == (int)crtc->height) {
                        MonitorGeometry first = monitors[0];
                        monitors[0] = monitors[j];
                        monitors[j] = first;
                        break;
                    }
                }
                XRRFreeCrtcInfo(crtc);
                break;
            }
            XRRFreeCrtcInfo(crtc);
        }
        XRRFreeScreenResources(resources);
        if (copied > 0) {
            return copied;
        }
    }

    int screen = DefaultScreen(display);
    monitors[0] = (MonitorGeometry){
        0, 0, DisplayWidth(display, screen), DisplayHeight(display, screen)
    };
    return 1;
}

long monitor_layout_event_mask(Display *display)
{
    int major = 1;
    int minor = 2;
    if (!XRRQueryVersion(display, &major, &minor)) {
        return 0;
    }
    long mask = RRScreenChangeNotifyMask;
    if (major > 1 || (major == 1 && minor >= 2)) {
        mask |= RRCrtcChangeNotifyMask | RROutputChangeNotifyMask;
    }
    if (major > 1 || (major == 1 && minor >= 4)) {
        mask |= RRResourceChangeNotifyMask;
    }
    return mask;
}

int monitor_layout_set_primary(Display *display, Window root, size_t index)
{
    int major = 1;
    int minor = 5;
    if (!XRRQueryVersion(display, &major, &minor) ||
        major < 1 || (major == 1 && minor < 3)) {
        fprintf(stderr, "Changing the primary display requires RandR 1.3 or newer.\n");
        return 0;
    }

    MonitorGeometry monitors[32];
    size_t count = monitor_layout_get(display, root, monitors,
                                      sizeof(monitors) / sizeof(monitors[0]));
    if (index >= count) {
        return 0;
    }

    int changed = 0;
    if (major > 1 || minor >= 5) {
        int active_count = 0;
        XRRMonitorInfo *active = XRRGetMonitors(display, root, True,
                                                 &active_count);
        if (active == NULL) {
            fprintf(stderr, "Could not read monitor outputs for primary display selection.\n");
            return 0;
        }
        for (int i = 0; i < active_count && !changed; ++i) {
            if (active[i].x != monitors[index].x ||
                active[i].y != monitors[index].y ||
                active[i].width != monitors[index].width ||
                active[i].height != monitors[index].height ||
                active[i].noutput == 0) {
                continue;
            }
            XRRSetOutputPrimary(display, root, active[i].outputs[0]);
            changed = 1;
        }
        XRRFreeMonitors(active);
    } else {
        XRRScreenResources *resources =
            XRRGetScreenResourcesCurrent(display, root);
        if (resources == NULL) {
            fprintf(stderr, "Could not read monitor outputs for primary display selection.\n");
            return 0;
        }
        for (int i = 0; i < resources->ncrtc && !changed; ++i) {
            XRRCrtcInfo *crtc =
                XRRGetCrtcInfo(display, resources, resources->crtcs[i]);
            if (crtc == NULL) {
                continue;
            }
            if (crtc->mode != None && crtc->noutput > 0 &&
                crtc->x == monitors[index].x &&
                crtc->y == monitors[index].y &&
                (int)crtc->width == monitors[index].width &&
                (int)crtc->height == monitors[index].height) {
                XRRSetOutputPrimary(display, root, crtc->outputs[0]);
                changed = 1;
            }
            XRRFreeCrtcInfo(crtc);
        }
        XRRFreeScreenResources(resources);
    }
    if (!changed) {
        fprintf(stderr, "Could not identify the selected display's output.\n");
    }
    return changed;
}
