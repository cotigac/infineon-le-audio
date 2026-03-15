/**
 * @file gatt_db.c
 * @brief GATT Database Implementation
 *
 * Implements GATT server with LE Audio services.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include "gatt_db.h"
#include <string.h>
#include <stdio.h>

/*******************************************************************************
 * Private Definitions
 ******************************************************************************/

/** Maximum connections for CCCD tracking */
#define MAX_CONNECTIONS                 4

/** Attribute types */
typedef enum {
    ATTR_TYPE_SERVICE,
    ATTR_TYPE_CHARACTERISTIC,
    ATTR_TYPE_CHAR_VALUE,
    ATTR_TYPE_DESCRIPTOR
} attr_type_t;

/*******************************************************************************
 * Private Types
 ******************************************************************************/

/** CCCD entry per connection */
typedef struct {
    bool in_use;
    uint16_t conn_handle;
    gatt_handle_t cccd_handle;
    uint16_t cccd_value;
} cccd_entry_t;

/** Attribute entry */
typedef struct {
    bool in_use;
    gatt_handle_t handle;
    attr_type_t type;
    gatt_uuid_t uuid;
    uint8_t properties;
    uint8_t permissions;
    uint8_t *value;
    uint16_t value_len;
    uint16_t max_len;
    gatt_read_callback_t read_cb;
    gatt_write_callback_t write_cb;
    void *user_data;
    gatt_handle_t cccd_handle;      /**< Associated CCCD handle (0 if none) */
} gatt_attribute_t;

/** Connection context */
typedef struct {
    bool in_use;
    uint16_t conn_handle;
    uint16_t mtu;
} connection_t;

/** GATT database context */
typedef struct {
    bool initialized;
    gatt_db_config_t config;
    gatt_db_handles_t handles;

    /* Attribute storage */
    gatt_attribute_t attributes[GATT_DB_MAX_CHARACTERISTICS * 3];
    uint16_t num_attributes;
    gatt_handle_t next_handle;

    /* CCCD storage */
    cccd_entry_t cccd_entries[GATT_DB_MAX_CCCD * MAX_CONNECTIONS];

    /* Connection tracking */
    connection_t connections[MAX_CONNECTIONS];

    /* Callbacks */
    gatt_db_callback_t event_callback;
    void *event_user_data;

    /* Service-specific callbacks */
    gatt_read_callback_t pacs_read_cb;
    void *pacs_user_data;

    gatt_read_callback_t ascs_read_cb;
    gatt_write_callback_t ascs_write_cb;
    void *ascs_user_data;

    gatt_read_callback_t bass_read_cb;
    gatt_write_callback_t bass_write_cb;
    void *bass_user_data;

    gatt_write_callback_t midi_write_cb;
    void *midi_user_data;

    /* Static value storage */
    char device_name[GATT_DB_MAX_DEVICE_NAME_LEN + 1];
    uint16_t appearance;
    uint8_t pnp_id[7];

} gatt_db_context_t;

/*******************************************************************************
 * Private Data
 ******************************************************************************/

static gatt_db_context_t db_ctx;

/* MIDI Service UUID */
static const uint8_t midi_service_uuid[] = GATT_UUID_MIDI_SERVICE;
static const uint8_t midi_io_uuid[] = GATT_UUID_MIDI_IO;

/*******************************************************************************
 * Private Function Prototypes
 ******************************************************************************/

static gatt_attribute_t* alloc_attribute(void);
static gatt_attribute_t* find_attribute(gatt_handle_t handle);
static gatt_handle_t add_service(const gatt_uuid_t *uuid, bool is_primary);
static gatt_handle_t add_characteristic(const gatt_uuid_t *uuid, uint8_t properties,
                                         uint8_t permissions, uint16_t max_len,
                                         gatt_read_callback_t read_cb,
                                         gatt_write_callback_t write_cb,
                                         void *user_data);
static gatt_handle_t add_cccd(void);
static gatt_handle_t add_descriptor(const gatt_uuid_t *uuid, uint8_t permissions,
                                     uint16_t max_len);

static connection_t* find_connection(uint16_t conn_handle);
static connection_t* alloc_connection(uint16_t conn_handle);
static void free_connection(uint16_t conn_handle);

static cccd_entry_t* find_cccd_entry(uint16_t conn_handle, gatt_handle_t cccd_handle);
static cccd_entry_t* alloc_cccd_entry(uint16_t conn_handle, gatt_handle_t cccd_handle);

static int build_gap_service(void);
static int build_gatt_service(void);
static int build_device_info_service(void);
static int build_pacs_service(void);
static int build_ascs_service(void);
static int build_bass_service(void);
static int build_midi_service(void);

static void notify_event(gatt_db_event_type_t type, uint16_t conn_handle, void *data);

/* Default read callbacks */
static uint8_t device_name_read(uint16_t conn_handle, gatt_handle_t attr_handle,
                                 uint8_t *data, uint16_t *len,
                                 uint16_t offset, void *user_data);
static uint8_t appearance_read(uint16_t conn_handle, gatt_handle_t attr_handle,
                                uint8_t *data, uint16_t *len,
                                uint16_t offset, void *user_data);
static uint8_t static_value_read(uint16_t conn_handle, gatt_handle_t attr_handle,
                                  uint8_t *data, uint16_t *len,
                                  uint16_t offset, void *user_data);

/*******************************************************************************
 * API Functions - Initialization
 ******************************************************************************/

int gatt_db_init(const gatt_db_config_t *config)
{
    if (db_ctx.initialized) {
        return GATT_DB_ERROR_ALREADY_INITIALIZED;
    }

    memset(&db_ctx, 0, sizeof(db_ctx));

    if (config != NULL) {
        memcpy(&db_ctx.config, config, sizeof(gatt_db_config_t));
    } else {
        /* Use defaults */
        gatt_db_config_t default_config = GATT_DB_CONFIG_DEFAULT;
        memcpy(&db_ctx.config, &default_config, sizeof(gatt_db_config_t));
    }

    /* Copy device name */
    if (db_ctx.config.device_name != NULL) {
        strncpy(db_ctx.device_name, db_ctx.config.device_name,
                GATT_DB_MAX_DEVICE_NAME_LEN);
    }

    db_ctx.appearance = db_ctx.config.appearance;
    db_ctx.next_handle = 1;  /* Handle 0 is reserved */

    /* Build standard services */
    build_gap_service();
    build_gatt_service();
    build_device_info_service();

    /* Build LE Audio services */
    if (db_ctx.config.enable_pacs) {
        build_pacs_service();
    }

    if (db_ctx.config.enable_ascs) {
        build_ascs_service();
    }

    if (db_ctx.config.enable_bass) {
        build_bass_service();
    }

    /* Build MIDI service */
    if (db_ctx.config.enable_midi) {
        build_midi_service();
    }

    /*
     * TODO: Register GATT database with BTSTACK
     *
     * btstack_gatt_server_register_database(db_ctx.attributes);
     */

    db_ctx.initialized = true;

    return GATT_DB_OK;
}

void gatt_db_deinit(void)
{
    if (!db_ctx.initialized) {
        return;
    }

    /* Free any allocated value buffers */
    for (int i = 0; i < db_ctx.num_attributes; i++) {
        if (db_ctx.attributes[i].value != NULL) {
            /* Note: In this implementation, values are static or stack-based */
        }
    }

    db_ctx.initialized = false;
}

void gatt_db_register_callback(gatt_db_callback_t callback, void *user_data)
{
    db_ctx.event_callback = callback;
    db_ctx.event_user_data = user_data;
}

const gatt_db_handles_t* gatt_db_get_handles(void)
{
    return &db_ctx.handles;
}

/*******************************************************************************
 * API Functions - Service Registration
 ******************************************************************************/

int gatt_db_add_service(const gatt_service_def_t *service,
                         gatt_handle_t *start_handle)
{
    gatt_handle_t svc_handle;

    if (!db_ctx.initialized) {
        return GATT_DB_ERROR_NOT_INITIALIZED;
    }

    if (service == NULL) {
        return GATT_DB_ERROR_INVALID_PARAM;
    }

    /* Add service declaration */
    svc_handle = add_service(&service->uuid, service->is_primary);
    if (svc_handle == GATT_INVALID_HANDLE) {
        return GATT_DB_ERROR_NO_RESOURCES;
    }

    if (start_handle != NULL) {
        *start_handle = svc_handle;
    }

    /* Add characteristics */
    for (int i = 0; i < service->num_chars; i++) {
        const gatt_char_def_t *chr = &service->chars[i];

        gatt_handle_t char_handle = add_characteristic(
            &chr->uuid, chr->properties, chr->permissions,
            chr->max_len, chr->read_cb, chr->write_cb, chr->user_data
        );

        if (char_handle == GATT_INVALID_HANDLE) {
            return GATT_DB_ERROR_NO_RESOURCES;
        }

        /* Add CCCD if needed */
        if (chr->has_cccd) {
            gatt_handle_t cccd_handle = add_cccd();
            if (cccd_handle != GATT_INVALID_HANDLE) {
                gatt_attribute_t *char_attr = find_attribute(char_handle);
                if (char_attr != NULL) {
                    char_attr->cccd_handle = cccd_handle;
                }
            }
        }
    }

    return GATT_DB_OK;
}

int gatt_db_register_pacs_callbacks(gatt_read_callback_t read_cb, void *user_data)
{
    db_ctx.pacs_read_cb = read_cb;
    db_ctx.pacs_user_data = user_data;
    return GATT_DB_OK;
}

int gatt_db_register_ascs_callbacks(gatt_read_callback_t read_cb,
                                     gatt_write_callback_t write_cb,
                                     void *user_data)
{
    db_ctx.ascs_read_cb = read_cb;
    db_ctx.ascs_write_cb = write_cb;
    db_ctx.ascs_user_data = user_data;
    return GATT_DB_OK;
}

int gatt_db_register_bass_callbacks(gatt_read_callback_t read_cb,
                                     gatt_write_callback_t write_cb,
                                     void *user_data)
{
    db_ctx.bass_read_cb = read_cb;
    db_ctx.bass_write_cb = write_cb;
    db_ctx.bass_user_data = user_data;
    return GATT_DB_OK;
}

int gatt_db_register_midi_callbacks(gatt_write_callback_t write_cb, void *user_data)
{
    db_ctx.midi_write_cb = write_cb;
    db_ctx.midi_user_data = user_data;
    return GATT_DB_OK;
}

/*******************************************************************************
 * API Functions - Attribute Operations
 ******************************************************************************/

int gatt_db_read_value(gatt_handle_t attr_handle, uint8_t *data,
                        uint16_t *len, uint16_t max_len)
{
    gatt_attribute_t *attr;

    if (!db_ctx.initialized) {
        return GATT_DB_ERROR_NOT_INITIALIZED;
    }

    attr = find_attribute(attr_handle);
    if (attr == NULL) {
        return GATT_DB_ERROR_INVALID_HANDLE;
    }

    if (attr->read_cb != NULL) {
        /* Use callback */
        return attr->read_cb(0, attr_handle, data, len, 0, attr->user_data);
    }

    /* Use stored value */
    if (attr->value == NULL || attr->value_len == 0) {
        *len = 0;
        return GATT_DB_OK;
    }

    uint16_t copy_len = (attr->value_len < max_len) ? attr->value_len : max_len;
    memcpy(data, attr->value, copy_len);
    *len = copy_len;

    return GATT_DB_OK;
}

int gatt_db_write_value(gatt_handle_t attr_handle, const uint8_t *data,
                         uint16_t len)
{
    gatt_attribute_t *attr;

    if (!db_ctx.initialized) {
        return GATT_DB_ERROR_NOT_INITIALIZED;
    }

    attr = find_attribute(attr_handle);
    if (attr == NULL) {
        return GATT_DB_ERROR_INVALID_HANDLE;
    }

    if (!(attr->permissions & (GATT_PERM_WRITE | GATT_PERM_WRITE_ENCRYPTED))) {
        return GATT_DB_ERROR_WRITE_NOT_PERMITTED;
    }

    if (attr->write_cb != NULL) {
        return attr->write_cb(0, attr_handle, data, len, 0, attr->user_data);
    }

    /* Store value */
    if (attr->value == NULL || len > attr->max_len) {
        return GATT_DB_ERROR_INSUFFICIENT_RESOURCES;
    }

    memcpy(attr->value, data, len);
    attr->value_len = len;

    return GATT_DB_OK;
}

int gatt_db_set_value(gatt_handle_t attr_handle, const uint8_t *data,
                       uint16_t len)
{
    gatt_attribute_t *attr;

    if (!db_ctx.initialized) {
        return GATT_DB_ERROR_NOT_INITIALIZED;
    }

    attr = find_attribute(attr_handle);
    if (attr == NULL) {
        return GATT_DB_ERROR_INVALID_HANDLE;
    }

    if (attr->value == NULL || len > attr->max_len) {
        return GATT_DB_ERROR_INSUFFICIENT_RESOURCES;
    }

    memcpy(attr->value, data, len);
    attr->value_len = len;

    return GATT_DB_OK;
}

/*******************************************************************************
 * API Functions - Notifications/Indications
 ******************************************************************************/

int gatt_db_send_notification(uint16_t conn_handle, gatt_handle_t attr_handle,
                               const uint8_t *data, uint16_t len)
{
    gatt_attribute_t *attr;

    if (!db_ctx.initialized) {
        return GATT_DB_ERROR_NOT_INITIALIZED;
    }

    attr = find_attribute(attr_handle);
    if (attr == NULL) {
        return GATT_DB_ERROR_INVALID_HANDLE;
    }

    /* Check if notifications are enabled */
    if (attr->cccd_handle != GATT_INVALID_HANDLE) {
        if (!gatt_db_notifications_enabled(conn_handle, attr->cccd_handle)) {
            return GATT_DB_OK;  /* Silently ignore if not enabled */
        }
    }

    /*
     * TODO: Send notification via BTSTACK
     *
     * btstack_gatt_server_send_notification(
     *     conn_handle,
     *     attr_handle,
     *     data,
     *     len
     * );
     */

    (void)data;
    (void)len;

    return GATT_DB_OK;
}

int gatt_db_send_indication(uint16_t conn_handle, gatt_handle_t attr_handle,
                             const uint8_t *data, uint16_t len)
{
    gatt_attribute_t *attr;

    if (!db_ctx.initialized) {
        return GATT_DB_ERROR_NOT_INITIALIZED;
    }

    attr = find_attribute(attr_handle);
    if (attr == NULL) {
        return GATT_DB_ERROR_INVALID_HANDLE;
    }

    /* Check if indications are enabled */
    if (attr->cccd_handle != GATT_INVALID_HANDLE) {
        if (!gatt_db_indications_enabled(conn_handle, attr->cccd_handle)) {
            return GATT_DB_OK;
        }
    }

    /*
     * TODO: Send indication via BTSTACK
     *
     * btstack_gatt_server_send_indication(
     *     conn_handle,
     *     attr_handle,
     *     data,
     *     len
     * );
     */

    (void)data;
    (void)len;

    return GATT_DB_OK;
}

bool gatt_db_notifications_enabled(uint16_t conn_handle, gatt_handle_t cccd_handle)
{
    cccd_entry_t *entry = find_cccd_entry(conn_handle, cccd_handle);
    if (entry == NULL) {
        return false;
    }
    return (entry->cccd_value & GATT_CCCD_NOTIFICATION) != 0;
}

bool gatt_db_indications_enabled(uint16_t conn_handle, gatt_handle_t cccd_handle)
{
    cccd_entry_t *entry = find_cccd_entry(conn_handle, cccd_handle);
    if (entry == NULL) {
        return false;
    }
    return (entry->cccd_value & GATT_CCCD_INDICATION) != 0;
}

/*******************************************************************************
 * API Functions - CCCD Management
 ******************************************************************************/

uint16_t gatt_db_get_cccd(uint16_t conn_handle, gatt_handle_t cccd_handle)
{
    cccd_entry_t *entry = find_cccd_entry(conn_handle, cccd_handle);
    if (entry == NULL) {
        return 0;
    }
    return entry->cccd_value;
}

int gatt_db_set_cccd(uint16_t conn_handle, gatt_handle_t cccd_handle,
                      uint16_t value)
{
    cccd_entry_t *entry = find_cccd_entry(conn_handle, cccd_handle);
    if (entry == NULL) {
        entry = alloc_cccd_entry(conn_handle, cccd_handle);
        if (entry == NULL) {
            return GATT_DB_ERROR_NO_RESOURCES;
        }
    }

    entry->cccd_value = value;

    /* Notify callback */
    if (db_ctx.config.cccd_callback != NULL) {
        db_ctx.config.cccd_callback(conn_handle, cccd_handle, value,
                                    db_ctx.config.user_data);
    }

    return GATT_DB_OK;
}

void gatt_db_clear_cccd(uint16_t conn_handle)
{
    for (int i = 0; i < GATT_DB_MAX_CCCD * MAX_CONNECTIONS; i++) {
        if (db_ctx.cccd_entries[i].in_use &&
            db_ctx.cccd_entries[i].conn_handle == conn_handle) {
            db_ctx.cccd_entries[i].in_use = false;
        }
    }
}

/*******************************************************************************
 * API Functions - Device Information
 ******************************************************************************/

int gatt_db_set_device_name(const char *name)
{
    if (!db_ctx.initialized) {
        return GATT_DB_ERROR_NOT_INITIALIZED;
    }

    if (name == NULL) {
        return GATT_DB_ERROR_INVALID_PARAM;
    }

    strncpy(db_ctx.device_name, name, GATT_DB_MAX_DEVICE_NAME_LEN);
    db_ctx.device_name[GATT_DB_MAX_DEVICE_NAME_LEN] = '\0';

    return GATT_DB_OK;
}

int gatt_db_set_appearance(uint16_t appearance)
{
    if (!db_ctx.initialized) {
        return GATT_DB_ERROR_NOT_INITIALIZED;
    }

    db_ctx.appearance = appearance;

    return GATT_DB_OK;
}

/*******************************************************************************
 * API Functions - MIDI Service
 ******************************************************************************/

int gatt_db_midi_send(uint16_t conn_handle, const uint8_t *data, uint16_t len)
{
    if (!db_ctx.initialized) {
        return GATT_DB_ERROR_NOT_INITIALIZED;
    }

    if (!db_ctx.config.enable_midi) {
        return GATT_DB_ERROR_NOT_FOUND;
    }

    return gatt_db_send_notification(conn_handle, db_ctx.handles.midi.midi_io,
                                     data, len);
}

/*******************************************************************************
 * API Functions - LE Audio Services
 ******************************************************************************/

int gatt_db_notify_sink_pac(uint16_t conn_handle, const uint8_t *data, uint16_t len)
{
    if (!db_ctx.initialized || !db_ctx.config.enable_pacs) {
        return GATT_DB_ERROR_NOT_INITIALIZED;
    }

    return gatt_db_send_notification(conn_handle, db_ctx.handles.pacs.sink_pac,
                                     data, len);
}

int gatt_db_notify_source_pac(uint16_t conn_handle, const uint8_t *data, uint16_t len)
{
    if (!db_ctx.initialized || !db_ctx.config.enable_pacs) {
        return GATT_DB_ERROR_NOT_INITIALIZED;
    }

    return gatt_db_send_notification(conn_handle, db_ctx.handles.pacs.source_pac,
                                     data, len);
}

int gatt_db_notify_available_contexts(uint16_t conn_handle,
                                       uint16_t sink_contexts,
                                       uint16_t source_contexts)
{
    uint8_t data[4];

    if (!db_ctx.initialized || !db_ctx.config.enable_pacs) {
        return GATT_DB_ERROR_NOT_INITIALIZED;
    }

    data[0] = sink_contexts & 0xFF;
    data[1] = (sink_contexts >> 8) & 0xFF;
    data[2] = source_contexts & 0xFF;
    data[3] = (source_contexts >> 8) & 0xFF;

    return gatt_db_send_notification(conn_handle,
                                     db_ctx.handles.pacs.available_contexts,
                                     data, 4);
}

int gatt_db_notify_ase_state(uint16_t conn_handle, uint8_t ase_id,
                              bool is_sink, const uint8_t *data, uint16_t len)
{
    gatt_handle_t handle;

    if (!db_ctx.initialized || !db_ctx.config.enable_ascs) {
        return GATT_DB_ERROR_NOT_INITIALIZED;
    }

    if (ase_id >= GATT_DB_MAX_ASE_COUNT) {
        return GATT_DB_ERROR_INVALID_PARAM;
    }

    if (is_sink) {
        handle = db_ctx.handles.ascs.ase_sink[ase_id];
    } else {
        handle = db_ctx.handles.ascs.ase_source[ase_id];
    }

    if (handle == GATT_INVALID_HANDLE) {
        return GATT_DB_ERROR_NOT_FOUND;
    }

    return gatt_db_send_notification(conn_handle, handle, data, len);
}

int gatt_db_notify_ase_cp(uint16_t conn_handle, const uint8_t *data, uint16_t len)
{
    if (!db_ctx.initialized || !db_ctx.config.enable_ascs) {
        return GATT_DB_ERROR_NOT_INITIALIZED;
    }

    return gatt_db_send_notification(conn_handle,
                                     db_ctx.handles.ascs.ase_control_point,
                                     data, len);
}

/*******************************************************************************
 * API Functions - Connection Events
 ******************************************************************************/

void gatt_db_on_connect(uint16_t conn_handle)
{
    connection_t *conn = alloc_connection(conn_handle);
    if (conn != NULL) {
        conn->mtu = 23;  /* Default ATT_MTU */
    }

    notify_event(GATT_DB_EVENT_CONNECTED, conn_handle, NULL);
}

void gatt_db_on_disconnect(uint16_t conn_handle)
{
    /* Clear CCCDs for this connection */
    gatt_db_clear_cccd(conn_handle);

    /* Free connection context */
    free_connection(conn_handle);

    notify_event(GATT_DB_EVENT_DISCONNECTED, conn_handle, NULL);
}

void gatt_db_on_mtu_changed(uint16_t conn_handle, uint16_t mtu)
{
    connection_t *conn = find_connection(conn_handle);
    if (conn != NULL) {
        conn->mtu = mtu;
    }

    notify_event(GATT_DB_EVENT_MTU_CHANGED, conn_handle, &mtu);
}

/*******************************************************************************
 * Utility Functions
 ******************************************************************************/

gatt_uuid_t gatt_uuid16(uint16_t uuid16)
{
    gatt_uuid_t uuid;
    uuid.type = 0;
    uuid.value.uuid16 = uuid16;
    return uuid;
}

gatt_uuid_t gatt_uuid128(const uint8_t *uuid128)
{
    gatt_uuid_t uuid;
    uuid.type = 1;
    memcpy(uuid.value.uuid128, uuid128, 16);
    return uuid;
}

bool gatt_uuid_equal(const gatt_uuid_t *a, const gatt_uuid_t *b)
{
    if (a->type != b->type) {
        return false;
    }

    if (a->type == 0) {
        return a->value.uuid16 == b->value.uuid16;
    } else {
        return memcmp(a->value.uuid128, b->value.uuid128, 16) == 0;
    }
}

/*******************************************************************************
 * Private Functions - Attribute Management
 ******************************************************************************/

static gatt_attribute_t* alloc_attribute(void)
{
    if (db_ctx.num_attributes >= sizeof(db_ctx.attributes) / sizeof(db_ctx.attributes[0])) {
        return NULL;
    }

    gatt_attribute_t *attr = &db_ctx.attributes[db_ctx.num_attributes++];
    memset(attr, 0, sizeof(gatt_attribute_t));
    attr->in_use = true;
    attr->handle = db_ctx.next_handle++;

    return attr;
}

static gatt_attribute_t* find_attribute(gatt_handle_t handle)
{
    for (int i = 0; i < db_ctx.num_attributes; i++) {
        if (db_ctx.attributes[i].handle == handle) {
            return &db_ctx.attributes[i];
        }
    }
    return NULL;
}

static gatt_handle_t add_service(const gatt_uuid_t *uuid, bool is_primary)
{
    gatt_attribute_t *attr = alloc_attribute();
    if (attr == NULL) {
        return GATT_INVALID_HANDLE;
    }

    attr->type = ATTR_TYPE_SERVICE;
    memcpy(&attr->uuid, uuid, sizeof(gatt_uuid_t));
    attr->permissions = GATT_PERM_READ;

    (void)is_primary;

    return attr->handle;
}

static gatt_handle_t add_characteristic(const gatt_uuid_t *uuid, uint8_t properties,
                                         uint8_t permissions, uint16_t max_len,
                                         gatt_read_callback_t read_cb,
                                         gatt_write_callback_t write_cb,
                                         void *user_data)
{
    /* Add characteristic declaration */
    gatt_attribute_t *decl = alloc_attribute();
    if (decl == NULL) {
        return GATT_INVALID_HANDLE;
    }

    decl->type = ATTR_TYPE_CHARACTERISTIC;
    decl->uuid = gatt_uuid16(0x2803);  /* Characteristic UUID */
    decl->properties = properties;
    decl->permissions = GATT_PERM_READ;

    /* Add characteristic value */
    gatt_attribute_t *value = alloc_attribute();
    if (value == NULL) {
        return GATT_INVALID_HANDLE;
    }

    value->type = ATTR_TYPE_CHAR_VALUE;
    memcpy(&value->uuid, uuid, sizeof(gatt_uuid_t));
    value->properties = properties;
    value->permissions = permissions;
    value->max_len = max_len;
    value->read_cb = read_cb;
    value->write_cb = write_cb;
    value->user_data = user_data;

    return value->handle;
}

static gatt_handle_t add_cccd(void)
{
    gatt_attribute_t *attr = alloc_attribute();
    if (attr == NULL) {
        return GATT_INVALID_HANDLE;
    }

    attr->type = ATTR_TYPE_DESCRIPTOR;
    attr->uuid = gatt_uuid16(GATT_UUID_CLIENT_CHAR_CONFIG);
    attr->permissions = GATT_PERM_READ | GATT_PERM_WRITE;
    attr->max_len = 2;

    return attr->handle;
}

static gatt_handle_t add_descriptor(const gatt_uuid_t *uuid, uint8_t permissions,
                                     uint16_t max_len)
{
    gatt_attribute_t *attr = alloc_attribute();
    if (attr == NULL) {
        return GATT_INVALID_HANDLE;
    }

    attr->type = ATTR_TYPE_DESCRIPTOR;
    memcpy(&attr->uuid, uuid, sizeof(gatt_uuid_t));
    attr->permissions = permissions;
    attr->max_len = max_len;

    return attr->handle;
}

/*******************************************************************************
 * Private Functions - Connection Management
 ******************************************************************************/

static connection_t* find_connection(uint16_t conn_handle)
{
    for (int i = 0; i < MAX_CONNECTIONS; i++) {
        if (db_ctx.connections[i].in_use &&
            db_ctx.connections[i].conn_handle == conn_handle) {
            return &db_ctx.connections[i];
        }
    }
    return NULL;
}

static connection_t* alloc_connection(uint16_t conn_handle)
{
    for (int i = 0; i < MAX_CONNECTIONS; i++) {
        if (!db_ctx.connections[i].in_use) {
            db_ctx.connections[i].in_use = true;
            db_ctx.connections[i].conn_handle = conn_handle;
            return &db_ctx.connections[i];
        }
    }
    return NULL;
}

static void free_connection(uint16_t conn_handle)
{
    connection_t *conn = find_connection(conn_handle);
    if (conn != NULL) {
        conn->in_use = false;
    }
}

/*******************************************************************************
 * Private Functions - CCCD Management
 ******************************************************************************/

static cccd_entry_t* find_cccd_entry(uint16_t conn_handle, gatt_handle_t cccd_handle)
{
    for (int i = 0; i < GATT_DB_MAX_CCCD * MAX_CONNECTIONS; i++) {
        if (db_ctx.cccd_entries[i].in_use &&
            db_ctx.cccd_entries[i].conn_handle == conn_handle &&
            db_ctx.cccd_entries[i].cccd_handle == cccd_handle) {
            return &db_ctx.cccd_entries[i];
        }
    }
    return NULL;
}

static cccd_entry_t* alloc_cccd_entry(uint16_t conn_handle, gatt_handle_t cccd_handle)
{
    for (int i = 0; i < GATT_DB_MAX_CCCD * MAX_CONNECTIONS; i++) {
        if (!db_ctx.cccd_entries[i].in_use) {
            db_ctx.cccd_entries[i].in_use = true;
            db_ctx.cccd_entries[i].conn_handle = conn_handle;
            db_ctx.cccd_entries[i].cccd_handle = cccd_handle;
            db_ctx.cccd_entries[i].cccd_value = 0;
            return &db_ctx.cccd_entries[i];
        }
    }
    return NULL;
}

/*******************************************************************************
 * Private Functions - Service Builders
 ******************************************************************************/

static int build_gap_service(void)
{
    gatt_handle_t svc = add_service(&(gatt_uuid_t){0, {.uuid16 = GATT_UUID_GAP_SERVICE}}, true);
    (void)svc;

    /* Device Name */
    add_characteristic(
        &(gatt_uuid_t){0, {.uuid16 = GATT_UUID_DEVICE_NAME}},
        GATT_PROP_READ,
        GATT_PERM_READ,
        GATT_DB_MAX_DEVICE_NAME_LEN,
        device_name_read, NULL, NULL
    );

    /* Appearance */
    add_characteristic(
        &(gatt_uuid_t){0, {.uuid16 = GATT_UUID_APPEARANCE}},
        GATT_PROP_READ,
        GATT_PERM_READ,
        2,
        appearance_read, NULL, NULL
    );

    return GATT_DB_OK;
}

static int build_gatt_service(void)
{
    add_service(&(gatt_uuid_t){0, {.uuid16 = GATT_UUID_GATT_SERVICE}}, true);

    /* Service Changed - optional but recommended */
    gatt_handle_t char_handle = add_characteristic(
        &(gatt_uuid_t){0, {.uuid16 = GATT_UUID_SERVICE_CHANGED}},
        GATT_PROP_INDICATE,
        0,
        4,
        NULL, NULL, NULL
    );
    add_cccd();

    (void)char_handle;

    return GATT_DB_OK;
}

static int build_device_info_service(void)
{
    db_ctx.handles.device_info.service = add_service(
        &(gatt_uuid_t){0, {.uuid16 = GATT_UUID_DEVICE_INFO_SERVICE}}, true
    );

    if (db_ctx.config.device_info.manufacturer_name != NULL) {
        db_ctx.handles.device_info.manufacturer_name = add_characteristic(
            &(gatt_uuid_t){0, {.uuid16 = GATT_UUID_MANUFACTURER_NAME}},
            GATT_PROP_READ,
            GATT_PERM_READ,
            64,
            static_value_read, NULL,
            (void*)db_ctx.config.device_info.manufacturer_name
        );
    }

    if (db_ctx.config.device_info.model_number != NULL) {
        db_ctx.handles.device_info.model_number = add_characteristic(
            &(gatt_uuid_t){0, {.uuid16 = GATT_UUID_MODEL_NUMBER}},
            GATT_PROP_READ,
            GATT_PERM_READ,
            32,
            static_value_read, NULL,
            (void*)db_ctx.config.device_info.model_number
        );
    }

    if (db_ctx.config.device_info.firmware_revision != NULL) {
        db_ctx.handles.device_info.firmware_revision = add_characteristic(
            &(gatt_uuid_t){0, {.uuid16 = GATT_UUID_FIRMWARE_REVISION}},
            GATT_PROP_READ,
            GATT_PERM_READ,
            32,
            static_value_read, NULL,
            (void*)db_ctx.config.device_info.firmware_revision
        );
    }

    /* PnP ID */
    db_ctx.pnp_id[0] = db_ctx.config.device_info.vendor_id_source;
    db_ctx.pnp_id[1] = db_ctx.config.device_info.vendor_id & 0xFF;
    db_ctx.pnp_id[2] = (db_ctx.config.device_info.vendor_id >> 8) & 0xFF;
    db_ctx.pnp_id[3] = db_ctx.config.device_info.product_id & 0xFF;
    db_ctx.pnp_id[4] = (db_ctx.config.device_info.product_id >> 8) & 0xFF;
    db_ctx.pnp_id[5] = db_ctx.config.device_info.product_version & 0xFF;
    db_ctx.pnp_id[6] = (db_ctx.config.device_info.product_version >> 8) & 0xFF;

    db_ctx.handles.device_info.pnp_id = add_characteristic(
        &(gatt_uuid_t){0, {.uuid16 = GATT_UUID_PNP_ID}},
        GATT_PROP_READ,
        GATT_PERM_READ,
        7,
        NULL, NULL, NULL
    );

    return GATT_DB_OK;
}

static int build_pacs_service(void)
{
    db_ctx.handles.pacs.service = add_service(
        &(gatt_uuid_t){0, {.uuid16 = GATT_UUID_PACS_SERVICE}}, true
    );

    /* Sink PAC */
    db_ctx.handles.pacs.sink_pac = add_characteristic(
        &(gatt_uuid_t){0, {.uuid16 = GATT_UUID_SINK_PAC}},
        GATT_PROP_READ | GATT_PROP_NOTIFY,
        GATT_PERM_READ,
        GATT_DB_MAX_VALUE_LEN,
        db_ctx.pacs_read_cb, NULL, db_ctx.pacs_user_data
    );
    db_ctx.handles.pacs.sink_pac_cccd = add_cccd();

    /* Sink Audio Locations */
    db_ctx.handles.pacs.sink_audio_locations = add_characteristic(
        &(gatt_uuid_t){0, {.uuid16 = GATT_UUID_SINK_AUDIO_LOCATIONS}},
        GATT_PROP_READ | GATT_PROP_NOTIFY,
        GATT_PERM_READ,
        4,
        db_ctx.pacs_read_cb, NULL, db_ctx.pacs_user_data
    );
    db_ctx.handles.pacs.sink_audio_locations_cccd = add_cccd();

    /* Source PAC */
    db_ctx.handles.pacs.source_pac = add_characteristic(
        &(gatt_uuid_t){0, {.uuid16 = GATT_UUID_SOURCE_PAC}},
        GATT_PROP_READ | GATT_PROP_NOTIFY,
        GATT_PERM_READ,
        GATT_DB_MAX_VALUE_LEN,
        db_ctx.pacs_read_cb, NULL, db_ctx.pacs_user_data
    );
    db_ctx.handles.pacs.source_pac_cccd = add_cccd();

    /* Source Audio Locations */
    db_ctx.handles.pacs.source_audio_locations = add_characteristic(
        &(gatt_uuid_t){0, {.uuid16 = GATT_UUID_SOURCE_AUDIO_LOCATIONS}},
        GATT_PROP_READ | GATT_PROP_NOTIFY,
        GATT_PERM_READ,
        4,
        db_ctx.pacs_read_cb, NULL, db_ctx.pacs_user_data
    );
    db_ctx.handles.pacs.source_audio_locations_cccd = add_cccd();

    /* Available Audio Contexts */
    db_ctx.handles.pacs.available_contexts = add_characteristic(
        &(gatt_uuid_t){0, {.uuid16 = GATT_UUID_AVAILABLE_AUDIO_CONTEXTS}},
        GATT_PROP_READ | GATT_PROP_NOTIFY,
        GATT_PERM_READ,
        4,
        db_ctx.pacs_read_cb, NULL, db_ctx.pacs_user_data
    );
    db_ctx.handles.pacs.available_contexts_cccd = add_cccd();

    /* Supported Audio Contexts */
    db_ctx.handles.pacs.supported_contexts = add_characteristic(
        &(gatt_uuid_t){0, {.uuid16 = GATT_UUID_SUPPORTED_AUDIO_CONTEXTS}},
        GATT_PROP_READ,
        GATT_PERM_READ,
        4,
        db_ctx.pacs_read_cb, NULL, db_ctx.pacs_user_data
    );

    return GATT_DB_OK;
}

static int build_ascs_service(void)
{
    db_ctx.handles.ascs.service = add_service(
        &(gatt_uuid_t){0, {.uuid16 = GATT_UUID_ASCS_SERVICE}}, true
    );

    /* Sink ASEs */
    uint8_t num_sink = db_ctx.config.num_sink_ases;
    if (num_sink > GATT_DB_MAX_ASE_COUNT) {
        num_sink = GATT_DB_MAX_ASE_COUNT;
    }
    db_ctx.handles.ascs.num_sink_ases = num_sink;

    for (int i = 0; i < num_sink; i++) {
        db_ctx.handles.ascs.ase_sink[i] = add_characteristic(
            &(gatt_uuid_t){0, {.uuid16 = GATT_UUID_ASE_SINK}},
            GATT_PROP_READ | GATT_PROP_NOTIFY,
            GATT_PERM_READ,
            64,
            db_ctx.ascs_read_cb, NULL, db_ctx.ascs_user_data
        );
        db_ctx.handles.ascs.ase_sink_cccd[i] = add_cccd();
    }

    /* Source ASEs */
    uint8_t num_source = db_ctx.config.num_source_ases;
    if (num_source > GATT_DB_MAX_ASE_COUNT) {
        num_source = GATT_DB_MAX_ASE_COUNT;
    }
    db_ctx.handles.ascs.num_source_ases = num_source;

    for (int i = 0; i < num_source; i++) {
        db_ctx.handles.ascs.ase_source[i] = add_characteristic(
            &(gatt_uuid_t){0, {.uuid16 = GATT_UUID_ASE_SOURCE}},
            GATT_PROP_READ | GATT_PROP_NOTIFY,
            GATT_PERM_READ,
            64,
            db_ctx.ascs_read_cb, NULL, db_ctx.ascs_user_data
        );
        db_ctx.handles.ascs.ase_source_cccd[i] = add_cccd();
    }

    /* ASE Control Point */
    db_ctx.handles.ascs.ase_control_point = add_characteristic(
        &(gatt_uuid_t){0, {.uuid16 = GATT_UUID_ASE_CONTROL_POINT}},
        GATT_PROP_WRITE | GATT_PROP_WRITE_NO_RSP | GATT_PROP_NOTIFY,
        GATT_PERM_WRITE,
        GATT_DB_MAX_VALUE_LEN,
        NULL, db_ctx.ascs_write_cb, db_ctx.ascs_user_data
    );
    db_ctx.handles.ascs.ase_control_point_cccd = add_cccd();

    return GATT_DB_OK;
}

static int build_bass_service(void)
{
    db_ctx.handles.bass.service = add_service(
        &(gatt_uuid_t){0, {.uuid16 = GATT_UUID_BASS_SERVICE}}, true
    );

    /* Broadcast Audio Scan Control Point */
    db_ctx.handles.bass.broadcast_scan_cp = add_characteristic(
        &(gatt_uuid_t){0, {.uuid16 = GATT_UUID_BROADCAST_AUDIO_SCAN_CP}},
        GATT_PROP_WRITE | GATT_PROP_WRITE_NO_RSP,
        GATT_PERM_WRITE,
        64,
        NULL, db_ctx.bass_write_cb, db_ctx.bass_user_data
    );
    db_ctx.handles.bass.broadcast_scan_cp_cccd = add_cccd();

    /* Broadcast Receive States */
    uint8_t num_states = db_ctx.config.num_broadcast_receive_states;
    if (num_states > GATT_DB_MAX_BROADCAST_RECEIVE_STATES) {
        num_states = GATT_DB_MAX_BROADCAST_RECEIVE_STATES;
    }
    db_ctx.handles.bass.num_receive_states = num_states;

    for (int i = 0; i < num_states; i++) {
        db_ctx.handles.bass.broadcast_receive_state[i] = add_characteristic(
            &(gatt_uuid_t){0, {.uuid16 = GATT_UUID_BROADCAST_RECEIVE_STATE}},
            GATT_PROP_READ | GATT_PROP_NOTIFY,
            GATT_PERM_READ,
            128,
            db_ctx.bass_read_cb, NULL, db_ctx.bass_user_data
        );
        db_ctx.handles.bass.broadcast_receive_state_cccd[i] = add_cccd();
    }

    return GATT_DB_OK;
}

static int build_midi_service(void)
{
    db_ctx.handles.midi.service = add_service(
        &(gatt_uuid_t){1, {.uuid128 = {0}}}, true
    );
    memcpy(db_ctx.attributes[db_ctx.num_attributes - 1].uuid.value.uuid128,
           midi_service_uuid, 16);

    /* MIDI I/O Characteristic */
    gatt_uuid_t midi_io = {1, {0}};
    memcpy(midi_io.value.uuid128, midi_io_uuid, 16);

    db_ctx.handles.midi.midi_io = add_characteristic(
        &midi_io,
        GATT_PROP_READ | GATT_PROP_WRITE_NO_RSP | GATT_PROP_NOTIFY,
        GATT_PERM_READ | GATT_PERM_WRITE,
        128,
        NULL, db_ctx.midi_write_cb, db_ctx.midi_user_data
    );
    db_ctx.handles.midi.midi_io_cccd = add_cccd();

    return GATT_DB_OK;
}

/*******************************************************************************
 * Private Functions - Event Notification
 ******************************************************************************/

static void notify_event(gatt_db_event_type_t type, uint16_t conn_handle, void *data)
{
    if (db_ctx.event_callback == NULL) {
        return;
    }

    gatt_db_event_t event;
    memset(&event, 0, sizeof(event));
    event.type = type;
    event.conn_handle = conn_handle;

    if (type == GATT_DB_EVENT_MTU_CHANGED && data != NULL) {
        event.data.mtu = *(uint16_t*)data;
    }

    db_ctx.event_callback(&event, db_ctx.event_user_data);
}

/*******************************************************************************
 * Private Functions - Default Read Callbacks
 ******************************************************************************/

static uint8_t device_name_read(uint16_t conn_handle, gatt_handle_t attr_handle,
                                 uint8_t *data, uint16_t *len,
                                 uint16_t offset, void *user_data)
{
    (void)conn_handle;
    (void)attr_handle;
    (void)user_data;

    uint16_t name_len = strlen(db_ctx.device_name);

    if (offset > name_len) {
        return ATT_ERROR_INVALID_OFFSET;
    }

    *len = name_len - offset;
    memcpy(data, db_ctx.device_name + offset, *len);

    return ATT_ERROR_SUCCESS;
}

static uint8_t appearance_read(uint16_t conn_handle, gatt_handle_t attr_handle,
                                uint8_t *data, uint16_t *len,
                                uint16_t offset, void *user_data)
{
    (void)conn_handle;
    (void)attr_handle;
    (void)user_data;

    if (offset > 2) {
        return ATT_ERROR_INVALID_OFFSET;
    }

    data[0] = db_ctx.appearance & 0xFF;
    data[1] = (db_ctx.appearance >> 8) & 0xFF;
    *len = 2 - offset;

    return ATT_ERROR_SUCCESS;
}

static uint8_t static_value_read(uint16_t conn_handle, gatt_handle_t attr_handle,
                                  uint8_t *data, uint16_t *len,
                                  uint16_t offset, void *user_data)
{
    (void)conn_handle;
    (void)attr_handle;

    const char *value = (const char*)user_data;
    if (value == NULL) {
        *len = 0;
        return ATT_ERROR_SUCCESS;
    }

    uint16_t value_len = strlen(value);

    if (offset > value_len) {
        return ATT_ERROR_INVALID_OFFSET;
    }

    *len = value_len - offset;
    memcpy(data, value + offset, *len);

    return ATT_ERROR_SUCCESS;
}

/*******************************************************************************
 * GATT Server Handlers (called from BT stack)
 ******************************************************************************/

/**
 * @brief Handle ATT read request
 */
uint8_t gatt_db_handle_read_request(uint16_t conn_handle,
                                     gatt_handle_t attr_handle,
                                     uint8_t *data, uint16_t *len,
                                     uint16_t offset)
{
    gatt_attribute_t *attr = find_attribute(attr_handle);
    if (attr == NULL) {
        return ATT_ERROR_INVALID_HANDLE;
    }

    if (!(attr->permissions & GATT_PERM_READ)) {
        return ATT_ERROR_READ_NOT_PERMITTED;
    }

    if (attr->read_cb != NULL) {
        return attr->read_cb(conn_handle, attr_handle, data, len, offset,
                             attr->user_data);
    }

    /* Return stored value */
    if (attr->value == NULL || attr->value_len == 0) {
        *len = 0;
        return ATT_ERROR_SUCCESS;
    }

    if (offset > attr->value_len) {
        return ATT_ERROR_INVALID_OFFSET;
    }

    *len = attr->value_len - offset;
    memcpy(data, attr->value + offset, *len);

    return ATT_ERROR_SUCCESS;
}

/**
 * @brief Handle ATT write request
 */
uint8_t gatt_db_handle_write_request(uint16_t conn_handle,
                                      gatt_handle_t attr_handle,
                                      const uint8_t *data, uint16_t len,
                                      uint16_t offset)
{
    gatt_attribute_t *attr = find_attribute(attr_handle);
    if (attr == NULL) {
        return ATT_ERROR_INVALID_HANDLE;
    }

    /* Check if this is a CCCD write */
    if (attr->type == ATTR_TYPE_DESCRIPTOR &&
        attr->uuid.type == 0 &&
        attr->uuid.value.uuid16 == GATT_UUID_CLIENT_CHAR_CONFIG) {

        if (len != 2) {
            return ATT_ERROR_INVALID_ATTRIBUTE_LENGTH;
        }

        uint16_t cccd_value = data[0] | (data[1] << 8);
        gatt_db_set_cccd(conn_handle, attr_handle, cccd_value);

        return ATT_ERROR_SUCCESS;
    }

    if (!(attr->permissions & GATT_PERM_WRITE)) {
        return ATT_ERROR_WRITE_NOT_PERMITTED;
    }

    if (attr->write_cb != NULL) {
        return attr->write_cb(conn_handle, attr_handle, data, len, offset,
                              attr->user_data);
    }

    /* Store value */
    if (offset + len > attr->max_len) {
        return ATT_ERROR_INVALID_ATTRIBUTE_LENGTH;
    }

    if (attr->value == NULL) {
        return ATT_ERROR_UNLIKELY_ERROR;
    }

    memcpy(attr->value + offset, data, len);
    if (offset + len > attr->value_len) {
        attr->value_len = offset + len;
    }

    return ATT_ERROR_SUCCESS;
}
