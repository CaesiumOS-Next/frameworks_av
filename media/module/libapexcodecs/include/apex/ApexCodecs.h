/*
 * Copyright (C) 2024 The Android Open Source Project
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *      http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

#pragma once

#include <sys/cdefs.h>
#include <errno.h>
#include <stdint.h>

#include <android/api-level.h>
#include <android/hardware_buffer.h>
#include <android/versioning.h>

__BEGIN_DECLS

/* A placeholder for when the next API level is available
 *      __INTRODUCED_IN(__ANDROID_API_FUTURE__) */
#define PLACEHOLDER_INTRO_IN_ANDROID_API_FUTURE

/**
 * Handle for component traits such as name, media type, kind (decoder/encoder),
 * domain (audio/video/image), etc.
 *
 * Introduced in API FUTURE.
 */
typedef struct ApexCodec_ComponentTraits ApexCodec_ComponentTraits;

/**
 * Error code for ApexCodec APIs.
 *
 * Introduced in API FUTURE.
 */
typedef enum codec_status_t : int32_t {
    CODEC_STATUS_OK        = 0,

    /* bad input */
    CODEC_STATUS_BAD_VALUE = EINVAL,
    CODEC_STATUS_BAD_INDEX = ENXIO,
    CODEC_STATUS_CANNOT_DO = ENOTSUP,

    /* bad sequencing of events */
    CODEC_STATUS_DUPLICATE = EEXIST,
    CODEC_STATUS_NOT_FOUND = ENOENT,
    CODEC_STATUS_BAD_STATE = EPERM,
    CODEC_STATUS_BLOCKING  = EWOULDBLOCK,
    CODEC_STATUS_CANCELED  = EINTR,

    /* bad environment */
    CODEC_STATUS_NO_MEMORY = ENOMEM,
    CODEC_STATUS_REFUSED   = EACCES,

    CODEC_STATUS_TIMED_OUT = ETIMEDOUT,

    /* bad versioning */
    CODEC_STATUS_OMITTED   = ENOSYS,

    /* unknown fatal */
    CODEC_STATUS_CORRUPTED = EFAULT,
    CODEC_STATUS_NO_INIT   = ENODEV,
} codec_status_t;

/**
 * Enum that represents the kind of component
 *
 * Introduced in API FUTURE.
 */
typedef enum ApexCodec_Kind : uint32_t {
    /**
     * The component is of a kind that is not listed below.
     */
    APEXCODEC_KIND_OTHER = 0x0,
    /**
     * The component is a decoder, which decodes coded bitstream
     * into raw buffers.
     *
     * Introduced in API FUTURE.
     */
    APEXCODEC_KIND_DECODER = 0x1,
    /**
     * The component is an encoder, which encodes raw buffers
     * into coded bitstream.
     *
     * Introduced in API FUTURE.
     */
    APEXCODEC_KIND_ENCODER = 0x2,
} ApexCodec_Kind;

typedef enum ApexCodec_Domain : uint32_t {
    /**
     * A component domain that is not listed below.
     *
     * Introduced in API FUTURE.
     */
    APEXCODEC_DOMAIN_OTHER = 0x0,
    /**
     * A component domain that operates on video.
     *
     * Introduced in API FUTURE.
     */
    APEXCODEC_DOMAIN_VIDEO = 0x1,
    /**
     * A component domain that operates on audio.
     *
     * Introduced in API FUTURE.
     */
    APEXCODEC_DOMAIN_AUDIO = 0x2,
    /**
     * A component domain that operates on image.
     *
     * Introduced in API FUTURE.
     */
    APEXCODEC_DOMAIN_IMAGE = 0x3,
} ApexCodec_Domain;

/**
 * An opaque struct that represents a component store.
 *
 * Introduced in API FUTURE.
 */
typedef struct ApexCodec_ComponentStore ApexCodec_ComponentStore;

/**
 * Get the component store object. The returned object can be
 *
 * \return component store object.
 */
ApexCodec_ComponentStore *ApexCodec_GetComponentStore()
        PLACEHOLDER_INTRO_IN_ANDROID_API_FUTURE;

/**
 * Get the traits object of a component at given index. ApexCodecs_Traits_*
 * functions are used to extract information from the traits object.
 *
 * Returns nullptr if index is out of bounds. The returned object is owned by
 * ApexCodec_ComponentStore object and the client should not delete it.
 *
 * \param index index of the traits object to query
 * \return traits object at the index, or nullptr if the index is out of bounds.
 */
ApexCodec_ComponentTraits *ApexCodec_Traits_get(
        ApexCodec_ComponentStore *store, size_t index) PLACEHOLDER_INTRO_IN_ANDROID_API_FUTURE;

/**
 * Get the name of the component from the traits object.
 *
 * The returned string is valid for the lifetime of the traits object,
 * and the client should not free the string.
 *
 * \param traits the traits object
 * \return the name of the component
 */
const char *ApexCodec_Traits_getComponentName(
        ApexCodec_ComponentTraits *traits)
        PLACEHOLDER_INTRO_IN_ANDROID_API_FUTURE;
/**
 * Get the supported media type of the component from the traits object.
 * Note that a component can support only one media type.
 *
 * The returned string is valid for the lifetime of the traits object,
 * and the client should not free the string.
 *
 * \param traits the traits object
 * \return the supported media type
 */
const char *ApexCodec_Traits_getMediaType(ApexCodec_ComponentTraits *traits)
        PLACEHOLDER_INTRO_IN_ANDROID_API_FUTURE;
/**
 * Get the kind of the component from the traits object. See ApexCodec_Kind for
 * the possible values.
 *
 * \param traits the traits object
 * \return the kind of the component
 */
ApexCodec_Kind ApexCodec_Traits_getKind(ApexCodec_ComponentTraits *traits)
        PLACEHOLDER_INTRO_IN_ANDROID_API_FUTURE;
/**
 * Get the domain on which the component operates from the traits object.
 * See ApexCodec_Domain for the possible values.
 *
 * \param traits the traits object
 * \return the domain that the component operates on
 */
ApexCodec_Domain ApexCodec_Traits_getDomain(ApexCodec_ComponentTraits *traits)
        PLACEHOLDER_INTRO_IN_ANDROID_API_FUTURE;

/**
 * An opaque struct that represents a codec.
 */
typedef struct ApexCodec_Component ApexCodec_Component;

/**
 * Create a component by the name. The created component should be ready to be configured and
 * to process frames.
 *
 * \param store the component store
 * \param name the name of the component
 * \param component out-param to be filled with the component; must not be null
 * \return  CODEC_STATUS_OK         if successful
 *          CODEC_STATUS_NOT_FOUND  if the name is not found
 */
codec_status_t ApexCodec_Component_create(
        ApexCodec_ComponentStore *store, const char *name, ApexCodec_Component **comp)
        PLACEHOLDER_INTRO_IN_ANDROID_API_FUTURE;

/**
 * Destroy the component by the handle. It is invalid to call component methods on the handle
 * after calling this method.
 *
 * \param comp the handle for the component
 */
void ApexCodec_Component_destroy(ApexCodec_Component *comp) PLACEHOLDER_INTRO_IN_ANDROID_API_FUTURE;

/**
 * Flush the component's internal states. This operation preserves the existing configurations.
 *
 * \param comp the handle for the component
 */
codec_status_t ApexCodec_Component_flush(
        ApexCodec_Component *comp) PLACEHOLDER_INTRO_IN_ANDROID_API_FUTURE;

/**
 * Resets the component to the initial state, right after creation. Note that the configuration
 * will also revert to the initial state, so if there are configurations required those should be
 * set again to use the component.
 *
 * \param comp the handle for the component
 */
codec_status_t ApexCodec_Component_reset(
        ApexCodec_Component *comp) PLACEHOLDER_INTRO_IN_ANDROID_API_FUTURE;

/**
 * An opaque struct that represents a configurable part of the component.
 *
 * Introduced in API FUTURE.
 */
typedef struct ApexCodec_Configurable ApexCodec_Configurable;

/**
 * Return the configurable object for the given ApexCodec_Component.
 * The returned object has the same lifecycle as |comp|.
 *
 * \param comp the handle for the component
 * \return the configurable object handle
 */
ApexCodec_Configurable *ApexCodec_Component_getConfigurable(
        ApexCodec_Component *comp) PLACEHOLDER_INTRO_IN_ANDROID_API_FUTURE;

/**
 * Enum that represents the flags for ApexCodec_Buffer.
 *
 * Introduced in API FUTURE.
 */
typedef enum codec_buffer_flags_t : uint32_t {
    CODEC_FLAG_DROP_FRAME    = (1 << 0),
    CODEC_FLAG_END_OF_STREAM = (1 << 1),
    CODEC_FLAG_DISCARD_FRAME = (1 << 2),
    CODEC_FLAG_INCOMPLETE    = (1 << 3),
    CODEC_FLAG_CORRECTED     = (1 << 4),
    CODEC_FLAG_CORRUPT       = (1 << 5),
    CODEC_FLAG_CODEC_CONFIG  = (1u << 31),
} codec_buffer_flags_t;

/**
 * Struct that represents the memory for ApexCodec_Buffer.
 *
 * All memory regions have the simple 1D representation.
 *
 * Introduced in API FUTURE.
 */
typedef struct ApexCodec_LinearBuffer {
    /**
     * A pointer to the start of the buffer. This is not aligned.
     */
    uint8_t *data;
    /**
     * Size of the buffer. The memory region between |data| and |data + size| is assumed to be
     * valid for read/write.
     */
    size_t size;
} ApexCodec_LinearBuffer;

/**
 * Struct that represents a buffer for ApexCodec_Component.
 *
 * Introduced in API FUTURE.
 */
typedef struct ApexCodec_Buffer {
    /**
     * Flags associated with the buffer.
     */
    codec_buffer_flags_t flags;
    /**
     * For input buffers client assign a unique sequential index for each buffer. For output buffers
     * it is the same as the associated input buffer's frame index.
     */
    uint64_t frameIndex;
    /**
     * A timestamp associated with the buffer.
     */
    uint64_t timestampUs;
    /**
     * The actual memory for the buffer. The domain, kind and direction (in/out) determines which
     * is used between linear and graphic.
     */
    union {
        ApexCodec_LinearBuffer linear;
        AHardwareBuffer *graphic;
    } memory;
    /**
     * Config updates associated with the buffer. For input buffers these are sent to the component
     * at the specific input frame. For output buffers these are config updates as a result of
     * processing the buffer.
     */
    uint8_t *configUpdates;
    /**
     * The size of the config updates.
     */
    size_t configUpdatesSize;
} ApexCodec_Buffer;

/**
 * Enum that represents the query type for the supported values.
 *
 * Introduced in API FUTURE.
 */
typedef enum ApexCodec_SupportedValuesQueryType : uint32_t {
    /** Query all possible supported values regardless of current configuration */
    APEXCODEC_SUPPORTED_VALUES_QUERY_POSSIBLE,
    /** Query supported values at current configuration */
    APEXCODEC_SUPPORTED_VALUES_QUERY_CURRENT,
} ApexCodec_SupportedValuesQueryType;

/**
 * Enum that represents the type of the supported values.
 *
 * Introduced in API FUTURE.
 */
typedef enum ApexCodec_SupportedValuesType : uint32_t {
    /** The supported values are empty. */
    APEXCODEC_SUPPORTED_VALUES_EMPTY,
    /** The supported values are represented by a range. */
    APEXCODEC_SUPPORTED_VALUES_RANGE,
    /** The supported values are represented by a list of values. */
    APEXCODEC_SUPPORTED_VALUES_VALUES,
    /** The supported values are represented by a list of flags. */
    APEXCODEC_SUPPORTED_VALUES_FLAGS,
} ApexCodec_SupportedValuesType;

/**
 * Enum that represents numeric types of the supported values.
 *
 * Introduced in API FUTURE.
 */
typedef enum ApexCodec_SupportedValuesNumberType : uint32_t {
    APEXCODEC_SUPPORTED_VALUES_TYPE_NONE   = 0,
    APEXCODEC_SUPPORTED_VALUES_TYPE_INT32  = 1,
    APEXCODEC_SUPPORTED_VALUES_TYPE_UINT32 = 2,
    // UNUSED                              = 3,
    APEXCODEC_SUPPORTED_VALUES_TYPE_INT64  = 4,
    APEXCODEC_SUPPORTED_VALUES_TYPE_UINT64 = 5,
    // UNUSED                              = 6,
    APEXCODEC_SUPPORTED_VALUES_TYPE_FLOAT  = 7,
} ApexCodec_SupportedValuesNumberType;

/**
 * Union of primitive types.
 *
 * Introduced in API FUTURE.
 */
typedef union {
    int32_t i32;
    uint32_t u32;
    int64_t i64;
    uint64_t u64;
    float f;
} ApexCodec_Value;

/**
 * An opaque struct that represents the supported values of a parameter.
 *
 * Introduced in API FUTURE.
 */
typedef struct ApexCodec_SupportedValues ApexCodec_SupportedValues;

/**
 * Extract information from ApexCodec_SupportedValues object.
 *
 * \param [in] supportedValues the supported values object
 * \param [out] type        pointer to be filled with the type of the supported values
 * \param [out] numberType  pointer to be filled with the numeric type of the supported values
 * \param [out] numValues   pointer to be filled with the number of values.
 *                          if type == APEXCODEC_SUPPORTED_VALUES_EMPTY: 0
 *                          if type == APEXCODEC_SUPPORTED_VALUES_RANGE: 5
 *                          if type == APEXCODEC_SUPPORTED_VALUES_VALUES/_FLAGS: varies
 * \param [out] values      pointer to be filled with the array of the actual supported values.
 *                          if type == APEXCODEC_SUPPORTED_VALUES_EMPTY: nullptr
 *                          if type == APEXCODEC_SUPPORTED_VALUES_RANGE: {min, max, step, num, den}
 *                          if type == APEXCODEC_SUPPORTED_VALUES_VALUES/_FLAGS:
 *                              the array of supported values/flags
 *                          the array is owned by the |supportedValues| object and the client
 *                          should not free it.
 */
codec_status_t ApexCodec_SupportedValues_getTypeAndValues(
        ApexCodec_SupportedValues *supportedValues,
        ApexCodec_SupportedValuesType *type,
        ApexCodec_SupportedValuesNumberType *numberType,
        uint32_t *numValues,
        ApexCodec_Value **values) PLACEHOLDER_INTRO_IN_ANDROID_API_FUTURE;

/**
 * Release the supported values object.
 *
 * \param values the supported values object
 */
void ApexCodec_SupportedValues_release(
        ApexCodec_SupportedValues *values) PLACEHOLDER_INTRO_IN_ANDROID_API_FUTURE;

/**
 * Struct that represents the result of ApexCodec_Configurable_config.
 *
 * Introduced in API FUTURE.
 */
typedef struct ApexCodec_SettingResult ApexCodec_SettingResult;

typedef struct ApexCodec_ParamFieldValues {
    /** index of the param */
    uint32_t index;
    /** offset of the param field */
    uint32_t offset;
    /** size of the param field */
    uint32_t size;
    /** currently supported values of the param field */
    ApexCodec_SupportedValues *values;
} ApexCodec_ParamFieldValues;

/**
 * Extract the result of ApexCodec_Configurable_config.
 *
 * \param [in]  result  the result object
 * \param [out] failure pointer to be filled with the failure code
 * \param [out] field   pointer to be filled with the field that failed.
 *                      |field->value| is owned by the |result| object and the client should not
 *                      free it.
 * \param [out] numConflicts pointer to be filled with the number of conflicts
 *                          may be 0 if there are no conflicts
 * \param [out] conflicts   pointer to be filled with the array of conflicts.
 *                          nullptr if |numConflicts| is 0.
 *                          the array and its content is owned by the |result| object and the client
 *                          should not free it.
 * \return CODEC_STATUS_OK         if successful
 */
codec_status_t ApexCodec_SettingResult_getResult(
        ApexCodec_SettingResult *result,
        int32_t *failure,
        ApexCodec_ParamFieldValues *field,
        size_t *numConflicts,
        ApexCodec_ParamFieldValues **conflicts) PLACEHOLDER_INTRO_IN_ANDROID_API_FUTURE;

/**
 * Release the setting result object
 *
 * \param result the setting result object
 */
void ApexCodec_SettingResult_release(
        ApexCodec_SettingResult *result) PLACEHOLDER_INTRO_IN_ANDROID_API_FUTURE;

/**
 * Process one frame from |input|, and produce one frame to |output| if possible.
 * When successfully filled, |output->memory.linear| has the size adjusted to the produced
 * output size, in case of linear buffers. |input->configUpdates| is applied with the input
 * buffer; |output->configUpdates| contains config updates as a result of processing the frame.
 *
 * \param comp      the component to process the buffers
 * \param input     the input buffer; when nullptr, the component should fill |output| if there are
 *                  any pending output buffers.
 * \param output    the output buffer, should not be nullptr.
 * \param consumed  the number of consumed bytes from the input buffer
 *                  set to 0 if no input buffer has been consumed, including |input| is nullptr.
 *                  for graphic buffers, any non-zero value means that the input buffer is consumed.
 * \return CODEC_STATUS_OK         if successful
 * \return CODEC_STATUS_NO_MEMORY  if the output buffer is too small to hold the output frame
 *                                 the client should retry with a larger output buffer;
 *                                 configUpdates should have the information to update
 *                                 the buffer size.
 * \return CODEC_STATUS_BAD_VALUE  if the parameters are bad
 * \return CODEC_STATUS_BAD_STATE  if the component is not in the right state to process the frame
 * \return CODEC_STATUS_CORRUPTED  if unexpected error has occurred
 */
codec_status_t ApexCodec_Component_process(
        ApexCodec_Component *comp,
        const ApexCodec_Buffer *input,
        ApexCodec_Buffer *output,
        size_t *consumed) PLACEHOLDER_INTRO_IN_ANDROID_API_FUTURE;

/**
 * Configure the component with the given config.
 *
 * Configurations are Codec 2.0 configs in binary blobs,
 * concatenated if there are multiple configs.
 *
 * \param [in] comp the handle for the component
 * \param [in] config the config blob
 * \param [in] configSize the size of the config blob
 * \param [out] result  the result of the configuration.
 *                      the client should call ApexCodec_SettingResult_getResult() to extract the
 *                      result. The result object is owned by the client and should be released
 *                      with ApexCodec_SettingResult_release().
 * \return CODEC_STATUS_OK         if successful
 * \return CODEC_STATUS_BAD_VALUE  if the config is invalid
 * \return CODEC_STATUS_BAD_STATE  if the component is not in the right state to be configured
 * \return CODEC_STATUS_CORRUPTED  if unexpected error has occurred
 */
codec_status_t ApexCodec_Configurable_config(
        ApexCodec_Configurable *comp,
        const uint8_t *config,
        size_t configSize,
        ApexCodec_SettingResult **result) PLACEHOLDER_INTRO_IN_ANDROID_API_FUTURE;

/**
 * Query the component for the given indices.
 *
 * \param comp the handle for the component
 * \param indices the array of indices to query
 * \param numIndices the size of the indices array
 * \param config the output buffer for the config blob, allocated by the client.
 * \param configSize the size of the config buffer; as an input it is set to the size of |config|;
 *                   as an output it is set to the written size of |config|, or the requested size
 *                   if the input size was insufficient.
 * \return CODEC_STATUS_OK          if successful
 * \return CODEC_STATUS_NO_MEMORY   if configSize is too small; |configSize| is updated to the
 *                                  requested buffer size.
 */
codec_status_t ApexCodec_Configurable_query(
        ApexCodec_Configurable *comp,
        uint32_t indices[],
        size_t numIndices,
        uint8_t *config,
        size_t *configSize) PLACEHOLDER_INTRO_IN_ANDROID_API_FUTURE;

/**
 * Query the component for the supported parameters.
 *
 * \param comp the handle for the component
 * \param indices the output buffer for the supported indices, allocated by the client.
 * \param numIndices the size of |indices|; as an input it is set to the size of |indices|;
 *                   as an output it is set to the written size of |indices|, or the requested size
 *                   if the input size was insufficient.
 * \return CODEC_STATUS_OK          if successful
 * \return CODEC_STATUS_NO_MEMORY   if |numIndices| is too small; |numIndices| is updated to the
 *                                  requested buffer size.
 */
codec_status_t ApexCodec_Configurable_querySupportedParams(
        ApexCodec_Configurable *comp,
        uint32_t *indices,
        size_t *numIndices) PLACEHOLDER_INTRO_IN_ANDROID_API_FUTURE;

/**
 * Struct that represents the query for the supported values of a parameter.
 *
 * Introduced in API FUTURE.
 */
typedef struct ApexCodec_SupportedValuesQuery {
    /* in-params */

    /** index of the param */
    uint32_t index;
    /** offset to the param field */
    size_t offset;
    /** query type */
    ApexCodec_SupportedValuesQueryType type;

    /* out-params */

    /** status of the query */
    codec_status_t status;

    /** supported values. must be released with ApexCodec_SupportedValues_release(). */
    ApexCodec_SupportedValues *values;
} ApexCodec_SupportedValuesQuery;

/**
 * Query the component for the supported values of the given indices.
 *
 * \param comp the handle for the component
 * \param queries the array of queries
 * \param numQueries the size of the queries array
 * \return  CODEC_STATUS_OK         if successful
 *          CODEC_STATUS_NO_MEMORY  if |queries[i].values| has not enough size for any |i|; the
 *                                  client can try again with larger buffer allocated for the values
 */
codec_status_t ApexCodec_Configurable_querySupportedValues(
        ApexCodec_Configurable *comp,
        ApexCodec_SupportedValuesQuery *queries,
        size_t numQueries) PLACEHOLDER_INTRO_IN_ANDROID_API_FUTURE;

__END_DECLS