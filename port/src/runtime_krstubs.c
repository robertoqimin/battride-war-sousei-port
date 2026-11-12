// SPDX-FileCopyrightText: Copyright 2026 kr-port contributors
// SPDX-License-Identifier: GPL-2.0-or-later
/* kr-port: system services this game imports that have no counterpart on a PC, answered so the
 * game takes its "feature not available" path instead of waiting:
 *   - system dialogs (error, sign-in, invitation, game custom data, USB storage, Facebook):
 *     same model as bbport's dialogs in runtime_services.c, an opened dialog has finished;
 *   - online features (NP in-game messages, Facebook): "signed out", as bbport's NP stubs;
 *   - USB storage: no device; hardware keyboard (sceImeKeyboard*): none connected;
 *   - share play, live streaming, screenshots, video recording, music player: accepted.
 * The first call of each function is logged, so a stub that matters shows up in the output. */
#include "runtime.h"
#include <stdio.h>
#include <string.h>

#define NP_SIGNED_OUT        ((int32_t)0x80550006)   /* as runtime_services.c */
#define DIALOG_NOT_INIT      ((int32_t)0x80B80003)
#define DIALOG_ALREADY_INIT  ((int32_t)0x80B80004)
#define NOT_AVAILABLE        ((int32_t)0x80020013)   /* ENODEV-style: no such device */

static void first_call(const char *name, int *seen) {
    if (!*seen) { *seen = 1; printf("Runtime: %s (kr-port stub)\n", name); }
}
#define STUB(fn, value) \
    static ABI int32_t fn(void) { static int seen; first_call(#fn, &seen); return value; }

/* ---- Dialogs: Initialize / Open / UpdateStatus / GetResult / Close / Terminate ---- */
typedef struct { const char *name; int initialized, status; } Dialog;
enum { D_ERROR, D_SIGNIN, D_INVITATION, D_CUSTOM_DATA, D_USB, D_FACEBOOK, D_COUNT };
static Dialog dialogs[D_COUNT] = {
    {"ErrorDialog", 0, 0}, {"SigninDialog", 0, 0}, {"InvitationDialog", 0, 0},
    {"GameCustomDataDialog", 0, 0}, {"UsbStorageDialog", 0, 0}, {"NpSnsFacebookDialog", 0, 0}};
static int32_t d_init(int i) {
    if (dialogs[i].initialized) return DIALOG_ALREADY_INIT;
    dialogs[i].initialized = 1; dialogs[i].status = 1; return 0;
}
static int32_t d_open(int i) {
    if (!dialogs[i].initialized) return DIALOG_NOT_INIT;
    printf("Runtime: %s opened; completed immediately (no dialog UI)\n", dialogs[i].name);
    dialogs[i].status = 3; return 0;
}
static int32_t d_term(int i) {
    if (!dialogs[i].initialized) return DIALOG_NOT_INIT;
    dialogs[i].initialized = 0; dialogs[i].status = 0; return 0;
}
/* Result: canceled by the user (result 1), so online/USB flows give up cleanly. */
static int32_t d_result(void *out) {
    if (!out) return DIALOG_NOT_INIT;
    int32_t *r = out; r[1] = 1; return 0;   /* { mode, result, ... } */
}
/* Not every dialog imports every entry point: the unused ones are fine. */
#define DIALOG(tag, i) \
    static ABI __attribute__((unused)) int32_t tag##_init(void) { return d_init(i); } \
    static ABI __attribute__((unused)) int32_t tag##_open(const void *p) { (void)p; return d_open(i); } \
    static ABI __attribute__((unused)) int32_t tag##_status(void) { return dialogs[i].status; } \
    static ABI __attribute__((unused)) int32_t tag##_result(void *r) { return d_result(r); } \
    static ABI __attribute__((unused)) int32_t tag##_close(void) { dialogs[i].status = 3; return 0; } \
    static ABI __attribute__((unused)) int32_t tag##_term(void) { return d_term(i); }
DIALOG(error, D_ERROR) DIALOG(signin, D_SIGNIN) DIALOG(invite, D_INVITATION)
DIALOG(custom, D_CUSTOM_DATA) DIALOG(usbdlg, D_USB) DIALOG(fbdlg, D_FACEBOOK)

/* ---- Hardware keyboard: none is connected ---- */
static ABI int32_t ime_kb_open(int32_t user, const void *param) {
    (void)user; (void)param; static int seen; first_call("sceImeKeyboardOpen", &seen);
    return NOT_AVAILABLE;
}
static ABI int32_t ime_kb_resource(int32_t user, uint32_t *id) {
    (void)user; if (id) *id = 0; return NOT_AVAILABLE;
}
static ABI int32_t ime_kb_info(uint32_t id, void *info) { (void)id; (void)info; return NOT_AVAILABLE; }
STUB(ime_kb_close, 0)
STUB(ime_update, 0)

/* ---- USB storage: no device ---- */
STUB(usb_init, 0) STUB(usb_term, 0) STUB(usb_register, 0) STUB(usb_unregister, 0)
static ABI int32_t usb_device_list(void *list) {
    static int seen; first_call("sceUsbStorageGetDeviceList", &seen);
    if (list) memset(list, 0, 4);   /* device count first: 0 */
    return 0;
}
STUB(usb_device_info, NOT_AVAILABLE) STUB(usb_is_exist, NOT_AVAILABLE)
STUB(usb_map, NOT_AVAILABLE) STUB(usb_unmap, 0)

/* ---- Online: signed out ---- */
STUB(igm_init, 0) STUB(igm_term, 0) STUB(igm_delete, 0)
STUB(igm_create, NP_SIGNED_OUT) STUB(igm_prepare, NP_SIGNED_OUT) STUB(igm_send, NP_SIGNED_OUT)
STUB(fb_create, NP_SIGNED_OUT) STUB(fb_token, NP_SIGNED_OUT) STUB(fb_delete, 0)
STUB(entitlement_key, NOT_AVAILABLE)

/* ---- Platform features with nothing to do here ---- */
STUB(shareplay_init, 0) STUB(shareplay_prohibit, 0)
STUB(streaming_init, 0) STUB(streaming_enable, 0)
STUB(screenshot_enable, 0) STUB(screenshot_disable, 0)
STUB(video_recording_info, 0)
STUB(music_disable, 0) STUB(music_reenable, 0)
STUB(pad_light_bar, 0)

/* ---- Network control, offline (bbport answers these only from its online module,
 * BB_ONLINE=1): initialized, disconnected, callbacks registered but never called. ---- */
#define NETCTL_NOT_CONNECTED ((int32_t)0x80412108)   /* as shadPS4 net_ctl_codes.h */
STUB(netctl_init, 0) STUB(netctl_term, 0) STUB(netctl_check_callback, 0)
static ABI int32_t netctl_get_state(int32_t *state) {
    static int seen; first_call("sceNetCtlGetState", &seen);
    if (state) *state = 0;   /* ORBIS_NET_CTL_STATE_DISCONNECTED */
    return 0;
}
static ABI int32_t netctl_register_callback(void *func, void *arg, int32_t *cid) {
    (void)func; (void)arg;
    if (cid) *cid = 1;
    return 0;
}
static ABI int32_t netctl_unregister_callback(int32_t cid) { (void)cid; return 0; }
static ABI int32_t netctl_get_result(int32_t event, int32_t *error) {
    (void)event; if (error) *error = NETCTL_NOT_CONNECTED; return 0;
}
static ABI int32_t netctl_get_info(int32_t code, void *info) { (void)code; (void)info; return NETCTL_NOT_CONNECTED; }
static ABI int32_t netctl_get_nat_info(void *info) { (void)info; return NETCTL_NOT_CONNECTED; }

static const RuntimeExport exports[] = {
    {"sceNetCtlInit", netctl_init}, {"sceNetCtlTerm", netctl_term},
    {"sceNetCtlGetState", netctl_get_state}, {"sceNetCtlCheckCallback", netctl_check_callback},
    {"sceNetCtlRegisterCallback", netctl_register_callback},
    {"sceNetCtlUnregisterCallback", netctl_unregister_callback},
    {"sceNetCtlGetResult", netctl_get_result}, {"sceNetCtlGetInfo", netctl_get_info},
    {"sceNetCtlGetNatInfo", netctl_get_nat_info},
    {"sceErrorDialogInitialize", error_init}, {"sceErrorDialogOpen", error_open},
    {"sceErrorDialogUpdateStatus", error_status}, {"sceErrorDialogClose", error_close},
    {"sceErrorDialogTerminate", error_term},
    {"sceSigninDialogInitialize", signin_init}, {"sceSigninDialogOpen", signin_open},
    {"sceSigninDialogUpdateStatus", signin_status}, {"sceSigninDialogGetResult", signin_result},
    {"sceSigninDialogClose", signin_close}, {"sceSigninDialogTerminate", signin_term},
    {"sceInvitationDialogInitialize", invite_init}, {"sceInvitationDialogOpen", invite_open},
    {"sceInvitationDialogUpdateStatus", invite_status}, {"sceInvitationDialogGetResult", invite_result},
    {"sceInvitationDialogTerminate", invite_term},
    {"sceGameCustomDataDialogInitialize", custom_init}, {"sceGameCustomDataDialogOpen", custom_open},
    {"sceGameCustomDataDialogUpdateStatus", custom_status}, {"sceGameCustomDataDialogGetResult", custom_result},
    {"sceGameCustomDataDialogTerminate", custom_term},
    {"sceUsbStorageDialogInitialize", usbdlg_init}, {"sceUsbStorageDialogOpen", usbdlg_open},
    {"sceUsbStorageDialogUpdateStatus", usbdlg_status}, {"sceUsbStorageDialogGetResult", usbdlg_result},
    {"sceUsbStorageDialogTerminate", usbdlg_term},
    {"sceNpSnsFacebookDialogUpdateStatus", fbdlg_status},
    {"sceNpSnsFacebookDialogInitialize", fbdlg_init}, {"sceNpSnsFacebookDialogOpen", fbdlg_open},
    {"sceNpSnsFacebookDialogGetResult", fbdlg_result}, {"sceNpSnsFacebookDialogTerminate", fbdlg_term},
    {"sceImeKeyboardOpen", ime_kb_open}, {"sceImeKeyboardClose", ime_kb_close},
    {"sceImeKeyboardGetResourceId", ime_kb_resource}, {"sceImeKeyboardGetInfo", ime_kb_info},
    {"sceImeUpdate", ime_update},
    {"sceUsbStorageInit", usb_init}, {"sceUsbStorageTerm", usb_term},
    {"sceUsbStorageRegisterCallback", usb_register}, {"sceUsbStorageUnregisterCallback", usb_unregister},
    {"sceUsbStorageGetDeviceList", usb_device_list}, {"sceUsbStorageGetDeviceInfo", usb_device_info},
    {"sceUsbStorageIsExist", usb_is_exist}, {"sceUsbStorageRequestMap", usb_map},
    {"sceUsbStorageRequestUnmap", usb_unmap},
    {"sceNpInGameMessageInitialize", igm_init}, {"sceNpInGameMessageTerminate", igm_term},
    {"sceNpInGameMessageCreateHandle", igm_create}, {"sceNpInGameMessageDeleteHandle", igm_delete},
    {"sceNpInGameMessagePrepare", igm_prepare}, {"sceNpInGameMessageSendData", igm_send},
    {"sceNpSnsFacebookCreateRequest", fb_create}, {"sceNpSnsFacebookGetAccessToken", fb_token},
    {"sceNpSnsFacebookDeleteRequest", fb_delete},
    {"sceAppContentGetEntitlementKey", entitlement_key},
    {"sceSharePlayInitialize", shareplay_init}, {"sceSharePlaySetProhibition", shareplay_prohibit},
    {"sceGameLiveStreamingInitialize", streaming_init},
    {"sceGameLiveStreamingEnableLiveStreaming", streaming_enable},
    {"sceScreenShotEnable", screenshot_enable}, {"sceScreenShotDisable", screenshot_disable},
    {"sceVideoRecordingSetInfo", video_recording_info},
    {"sceSystemServiceDisableMusicPlayer", music_disable},
    {"sceSystemServiceReenableMusicPlayer", music_reenable},
    {"scePadSetLightBar", pad_light_bar},
};
uintptr_t runtime_krstubs_resolve(const char *name) {
    return RUNTIME_LOOKUP(exports, name);
}
