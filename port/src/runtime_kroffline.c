// SPDX-FileCopyrightText: Copyright 2026 kr-port contributors
// SPDX-License-Identifier: GPL-2.0-or-later
/* kr-port: the online services this game imports, answered as a console that is signed out of
 * PSN (bbport's NP stubs do the same for the rest), so every online feature takes its offline
 * path. Requests and contexts cannot be created; deletes and shutdowns succeed.
 *   - NP Score (rankings), TUS/TSS (online storage), Matching2 (rooms), WebApi, word filter,
 *     bandwidth test, lookup, auth: "signed out";
 *   - libSceJson2 (sce::Json, C++): only reached with server data, which never arrives offline.
 *     Constructors, assignments and increments return `this`; getters return 0 / empty; lookups
 *     return a shared zeroed value; iterators compare equal (empty containers); parse fails;
 *   - sceHttpUriEscape: real percent-encoding (RFC 3986 unreserved characters kept);
 *   - sceNpCommerceDialogGetResult: canceled; DiscMap: no bitmap info (as shadPS4).
 * The first call of each stub is logged. */
#include "runtime.h"
#include <stdio.h>
#include <string.h>

#define NP_SIGNED_OUT           ((int32_t)0x80550006)   /* shadPS4 np_error.h */
#define MATCHING2_SIGNED_OUT    ((int32_t)0x80550C26)
#define JSON_PARSE_ERROR        ((int32_t)0x80920002)   /* not verified: any negative fails */
#define HTTP_INVALID_VALUE      ((int32_t)0x804311FE)   /* not verified */
#define HTTP_OUT_OF_SIZE        ((int32_t)0x80431104)   /* not verified */
#define DISC_MAP_NO_BITMAP_INFO ((int32_t)0x81100004)   /* shadPS4 disc_map_codes.h */

static void first_call(const char *name, int *seen) {
    if (!*seen) { *seen = 1; printf("Runtime: %s (kr-port offline)\n", name); }
}
#define RET(fn, value) \
    static ABI int32_t fn(void) { static int seen; first_call(#fn, &seen); return value; }

/* ---- NP: signed out ---- */
RET(np_signed_out, NP_SIGNED_OUT)
RET(np_matching2_signed_out, MATCHING2_SIGNED_OUT)
RET(np_ok, 0)

/* ---- sce::Json ---- */
static unsigned char zero_value[256];   /* a null Value / empty String / empty container */
static ABI void *json_self(void *self) { return self; }
static ABI int32_t json_zero(void) { return 0; }
static ABI void json_void(void) {}
static ABI void *json_zero_ref(void) { memset(zero_value, 0, sizeof(zero_value)); return zero_value; }
static ABI const char *json_c_str(void) { return ""; }
static ABI int32_t json_compare(void) { return 1; }      /* String::compare: never equal */
static ABI int32_t json_parse(void) { static int seen; first_call("sce::Json::Parser::parse", &seen); return JSON_PARSE_ERROR; }

/* ---- sceHttpUriEscape(out, *require, prepare, in) ---- */
static ABI int32_t http_uri_escape(char *out, uint64_t *require, uint64_t prepare, const char *in) {
    if (!in) return HTTP_INVALID_VALUE;
    static const char hex[] = "0123456789ABCDEF";
    uint64_t need = 1;
    for (const unsigned char *p = (const unsigned char *)in; *p; ++p) {
        int keep = (*p >= 'A' && *p <= 'Z') || (*p >= 'a' && *p <= 'z') || (*p >= '0' && *p <= '9') ||
                   *p == '-' || *p == '_' || *p == '.' || *p == '~';
        need += keep ? 1 : 3;
    }
    if (require) *require = need;
    if (!out) return 0;
    if (prepare < need) return HTTP_OUT_OF_SIZE;
    char *o = out;
    for (const unsigned char *p = (const unsigned char *)in; *p; ++p) {
        int keep = (*p >= 'A' && *p <= 'Z') || (*p >= 'a' && *p <= 'z') || (*p >= '0' && *p <= '9') ||
                   *p == '-' || *p == '_' || *p == '.' || *p == '~';
        if (keep) *o++ = (char)*p;
        else { *o++ = '%'; *o++ = hex[*p >> 4]; *o++ = hex[*p & 15]; }
    }
    *o = 0;
    return 0;
}

/* ---- Commerce dialog: canceled; DiscMap ---- */
static ABI int32_t commerce_result(int32_t *result) {
    if (result) result[0] = 1;   /* SCE_COMMON_DIALOG_RESULT_USER_CANCELED */
    return 0;
}
static ABI int32_t disc_map_request(void) { return DISC_MAP_NO_BITMAP_INFO; }

static const RuntimeExport exports[] = {
    /* NP Score */
    {"sceNpScoreGetFriendsRanking", np_signed_out}, {"sceNpScoreGetFriendsRankingAsync", np_signed_out},
    {"sceNpScoreGetGameDataAsync", np_signed_out}, {"sceNpScoreGetRankingByNpId", np_signed_out},
    {"sceNpScoreGetRankingByNpIdAsync", np_signed_out}, {"sceNpScoreGetRankingByRangeAsync", np_signed_out},
    {"sceNpScorePollAsync", np_signed_out}, {"sceNpScoreRecordGameDataAsync", np_signed_out},
    {"sceNpScoreRecordScoreAsync", np_signed_out},
    /* TUS / TSS */
    {"sceNpTssGetData", np_signed_out}, {"sceNpTusAddAndGetVariable", np_signed_out},
    {"sceNpTusAddAndGetVariableVUser", np_signed_out}, {"sceNpTusCreateNpTitleCtx", np_signed_out},
    {"sceNpTusCreateRequest", np_signed_out}, {"sceNpTusDeleteMultiSlotData", np_signed_out},
    {"sceNpTusDeleteMultiSlotDataVUser", np_signed_out}, {"sceNpTusDeleteNpTitleCtx", np_ok},
    {"sceNpTusDeleteRequest", np_ok}, {"sceNpTusGetData", np_signed_out}, {"sceNpTusGetDataVUser", np_signed_out},
    {"sceNpTusGetMultiSlotVariable", np_signed_out}, {"sceNpTusGetMultiSlotVariableVUser", np_signed_out},
    {"sceNpTusSetData", np_signed_out}, {"sceNpTusSetDataVUser", np_signed_out},
    {"sceNpTusSetMultiSlotVariable", np_signed_out}, {"sceNpTusSetMultiSlotVariableVUser", np_signed_out},
    /* Matching2 */
    {"sceNpMatching2GetRoomDataExternalList", np_matching2_signed_out},
    {"sceNpMatching2GetRoomDataInternal", np_matching2_signed_out},
    {"sceNpMatching2GetRoomMemberDataInternal", np_matching2_signed_out},
    {"sceNpMatching2GetUserInfoList", np_matching2_signed_out},
    {"sceNpMatching2RegisterRoomMessageCallback", np_ok},
    {"sceNpMatching2SendRoomChatMessage", np_matching2_signed_out},
    {"sceNpMatching2SendRoomMessage", np_matching2_signed_out},
    {"sceNpMatching2SetUserInfo", np_matching2_signed_out},
    {"sceNpMatching2SignalingGetConnectionInfo", np_matching2_signed_out},
    /* WebApi */
    {"sceNpWebApiCreateHandle", np_signed_out}, {"sceNpWebApiDeleteHandle", np_ok},
    {"sceNpWebApiCreateServicePushEventFilter", np_signed_out}, {"sceNpWebApiDeleteServicePushEventFilter", np_ok},
    {"sceNpWebApiRegisterServicePushEventCallback", np_signed_out},
    {"sceNpWebApiUnregisterServicePushEventCallback", np_ok}, {"sceNpWebApiSendRequest2", np_signed_out},
    /* Utility: bandwidth test, lookup, word filter */
    {"sceNpBandwidthTestInitStart", np_signed_out}, {"sceNpBandwidthTestGetStatus", np_signed_out},
    {"sceNpBandwidthTestShutdown", np_ok}, {"sceNpLookupCreateRequest", np_signed_out},
    {"sceNpWordFilterCreateTitleCtx", np_signed_out}, {"sceNpWordFilterDeleteTitleCtx", np_ok},
    {"sceNpWordFilterCreateRequest", np_signed_out}, {"sceNpWordFilterCreateAsyncRequest", np_signed_out},
    {"sceNpWordFilterDeleteRequest", np_ok}, {"sceNpWordFilterAbortRequest", np_ok},
    {"sceNpWordFilterCensorComment", np_signed_out}, {"sceNpWordFilterSanitizeComment", np_signed_out},
    {"sceNpWordFilterPollAsync", np_signed_out},
    /* Requests */
    {"sceNpCreateRequest", np_signed_out}, {"sceNpAuthCreateRequest", np_signed_out},
    /* Others */
    {"sceHttpUriEscape", http_uri_escape}, {"sceNpCommerceDialogGetResult", commerce_result},
    /* sce::Json (libSceJson2) */
    {"_ZN3sce4Json11Initializer10initializeEPKNS0_13InitParameterE", json_zero},
    {"_ZN3sce4Json11Initializer9terminateEv", json_zero},
    {"_ZN3sce4Json11InitializerC1Ev", json_self}, {"_ZN3sce4Json11InitializerD1Ev", json_self},
    {"_ZN3sce4Json12MemAllocator11notifyErrorEimPv", json_void},
    {"_ZN3sce4Json12MemAllocatorC2Ev", json_self}, {"_ZN3sce4Json12MemAllocatorD2Ev", json_self},
    {"_ZN3sce4Json5Array8iteratorD1Ev", json_self}, {"_ZN3sce4Json5Array8iteratorppEi", json_self},
    {"_ZN3sce4Json5Array8iteratorppEv", json_self}, {"_ZN3sce4Json5ArrayC1ERKS1_", json_self},
    {"_ZN3sce4Json5ArrayC1Ev", json_self}, {"_ZN3sce4Json5ArrayD1Ev", json_self},
    {"_ZN3sce4Json5ArrayaSERKS1_", json_self}, {"_ZN3sce4Json5Value5clearEv", json_void},
    {"_ZN3sce4Json5ValueC1ERKNS0_6ObjectE", json_self}, {"_ZN3sce4Json5ValueC1ERKS1_", json_self},
    {"_ZN3sce4Json5ValueC1Ev", json_self}, {"_ZN3sce4Json5ValueD1Ev", json_self},
    {"_ZN3sce4Json5ValueaSERKS1_", json_self}, {"_ZN3sce4Json6Object8iteratorC1Ev", json_self},
    {"_ZN3sce4Json6Object8iteratorD1Ev", json_self}, {"_ZN3sce4Json6Object8iteratoraSERKS2_", json_self},
    {"_ZN3sce4Json6Object8iteratorppEi", json_self}, {"_ZN3sce4Json6Object8iteratorppEv", json_self},
    {"_ZN3sce4Json6ObjectC1ERKS1_", json_self}, {"_ZN3sce4Json6ObjectC1Ev", json_self},
    {"_ZN3sce4Json6ObjectD1Ev", json_self}, {"_ZN3sce4Json6ObjectaSERKS1_", json_self},
    {"_ZN3sce4Json6ObjectixERKNS0_6StringE", json_zero_ref},
    {"_ZN3sce4Json6Parser5parseERNS0_5ValueEPKcm", json_parse},
    {"_ZN3sce4Json6StringC1EPKc", json_self}, {"_ZN3sce4Json6StringC1ERKS1_", json_self},
    {"_ZN3sce4Json6StringC1Ev", json_self}, {"_ZN3sce4Json6StringD1Ev", json_self},
    {"_ZN3sce4Json6StringaSERKS1_", json_self}, {"_ZN3sce4Json6StringpLEPKc", json_self},
    {"_ZNK3sce4Json5Array3endEv", json_self}, {"_ZNK3sce4Json5Array4sizeEv", json_zero},
    {"_ZNK3sce4Json5Array5beginEv", json_self}, {"_ZNK3sce4Json5Array8iteratordeEv", json_zero_ref},
    {"_ZNK3sce4Json5Array8iteratorneERKS2_", json_zero}, {"_ZNK3sce4Json5Array8iteratorptEv", json_zero_ref},
    {"_ZNK3sce4Json5Value10getBooleanEv", json_zero}, {"_ZNK3sce4Json5Value10getIntegerEv", json_zero},
    {"_ZNK3sce4Json5Value11getUIntegerEv", json_zero}, {"_ZNK3sce4Json5Value7getTypeEv", json_zero},
    {"_ZNK3sce4Json5Value8getArrayEv", json_zero_ref}, {"_ZNK3sce4Json5Value8toStringERNS0_6StringE", json_zero},
    {"_ZNK3sce4Json5Value9getObjectEv", json_zero_ref}, {"_ZNK3sce4Json5Value9getStringEv", json_zero_ref},
    {"_ZNK3sce4Json5ValueixEPKc", json_zero_ref}, {"_ZNK3sce4Json5ValueixEm", json_zero_ref},
    {"_ZNK3sce4Json6Object3endEv", json_self}, {"_ZNK3sce4Json6Object5beginEv", json_self},
    {"_ZNK3sce4Json6Object8iteratorneERKS2_", json_zero}, {"_ZNK3sce4Json6Object8iteratorptEv", json_zero_ref},
    {"_ZNK3sce4Json6String4sizeEv", json_zero}, {"_ZNK3sce4Json6String5c_strEv", json_c_str},
    {"_ZNK3sce4Json6String6lengthEv", json_zero}, {"_ZNK3sce4Json6String7compareEPKc", json_compare},
    {"_ZNK3sce4Json6StringeqEPKc", json_zero},
};
uintptr_t runtime_kroffline_resolve(const char *name) {
    if (runtime_nid_is(name, "ioKMruft1ek")) return (uintptr_t)disc_map_request;   /* libSceDiscMap */
    return RUNTIME_LOOKUP(exports, name);
}
