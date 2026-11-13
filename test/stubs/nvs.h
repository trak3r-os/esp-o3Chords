#pragma once
#include <cstdint>
#include <cstddef>
#include <cstring>
#include <map>
#include <string>
#include <vector>
#include "esp_err.h"
typedef uint32_t nvs_handle_t;
typedef void* nvs_iterator_t;
typedef int nvs_type_t;
struct nvs_entry_info_t { char key[16]; };
#define NVS_DEFAULT_PART_NAME "nvs"
#define NVS_TYPE_BLOB 1
#define NVS_READONLY  0
#define NVS_READWRITE 1
constexpr esp_err_t ESP_ERR_NVS_NOT_FOUND = 0x1101;
constexpr esp_err_t ESP_ERR_NVS_NEW_VERSION_FOUND = 0x1102;
constexpr esp_err_t ESP_ERR_NVS_NO_FREE_PAGES = 0x1103;
namespace nvs_stub { inline std::map<std::string, std::vector<uint8_t>> blobs; inline esp_err_t query_error=ESP_OK, read_error=ESP_OK, set_error=ESP_OK, commit_error=ESP_OK; inline void clear(){ blobs.clear(); query_error=read_error=set_error=commit_error=ESP_OK; } }
static inline esp_err_t nvs_open(const char*, int, nvs_handle_t* h) { *h = 1; return ESP_OK; }
static inline esp_err_t nvs_close(nvs_handle_t) { return ESP_OK; }
static inline esp_err_t nvs_set_blob(nvs_handle_t, const char* key, const void* data, size_t size) { if(nvs_stub::set_error!=ESP_OK)return nvs_stub::set_error; nvs_stub::blobs[key]=std::vector<uint8_t>((const uint8_t*)data,(const uint8_t*)data+size); return ESP_OK; }
static inline esp_err_t nvs_get_blob(nvs_handle_t, const char* key, void* data, size_t* size) { auto it=nvs_stub::blobs.find(key); if(it==nvs_stub::blobs.end())return ESP_ERR_NVS_NOT_FOUND; if(!data){if(nvs_stub::query_error!=ESP_OK)return nvs_stub::query_error;*size=it->second.size();return ESP_OK;} if(nvs_stub::read_error!=ESP_OK)return nvs_stub::read_error; if(*size<it->second.size())return ESP_FAIL; std::memcpy(data,it->second.data(),it->second.size());*size=it->second.size();return ESP_OK; }
static inline esp_err_t nvs_commit(nvs_handle_t) { return nvs_stub::commit_error; }
static inline esp_err_t nvs_erase_key(nvs_handle_t, const char* key) { nvs_stub::blobs.erase(key); return ESP_OK; }
static inline esp_err_t nvs_erase_all(nvs_handle_t) { nvs_stub::blobs.clear(); return ESP_OK; }
static inline esp_err_t nvs_entry_find(const char*, const char*, nvs_type_t, nvs_iterator_t*) { return ESP_ERR_NVS_NOT_FOUND; }
static inline void nvs_entry_info(nvs_iterator_t, nvs_entry_info_t*) {}
static inline esp_err_t nvs_entry_next(nvs_iterator_t*) { return ESP_ERR_NVS_NOT_FOUND; }
static inline void nvs_release_iterator(nvs_iterator_t) {}
