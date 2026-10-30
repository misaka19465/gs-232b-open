#include "gs232b/ports.h"

#include <string.h>

static bool domain_valid(gs232b_store_domain_t domain)
{
    return domain >= GS232B_STORE_NETWORK && domain < GS232B_STORE_DOMAIN_COUNT;
}

gs232b_error_t gs232b_sampler_read(const gs232b_sampler_t *sampler,
                                  gs232b_axis_id_t axis, gs232b_sample_t *sample)
{
    if (sample == NULL) {
        return GS232B_ERROR_INVALID_ARGUMENT;
    }
    memset(sample, 0, sizeof(*sample));
    if (sampler == NULL || sampler->read == NULL ||
        axis < GS232B_AXIS_AZIMUTH || axis >= GS232B_AXIS_COUNT) {
        return GS232B_ERROR_INVALID_ARGUMENT;
    }
    gs232b_sample_t next = {0};
    gs232b_error_t error = sampler->read(sampler->ctx, axis, &next);
    if (error != GS232B_OK) {
        return error;
    }
    if (!next.valid) {
        return GS232B_ERROR_NOT_READY;
    }
    *sample = next;
    return GS232B_OK;
}

gs232b_error_t gs232b_output_apply(const gs232b_output_t *output,
                                  const gs232b_output_frame_t *frame)
{
    if (output == NULL || output->apply == NULL || !gs232b_output_frame_valid(frame)) {
        return GS232B_ERROR_INVALID_ARGUMENT;
    }
    return output->apply(output->ctx, frame);
}

gs232b_error_t gs232b_store_load(const gs232b_store_t *store,
                                gs232b_store_domain_t domain,
                                uint8_t *buffer, size_t capacity, size_t *size)
{
    if (size == NULL) {
        return GS232B_ERROR_INVALID_ARGUMENT;
    }
    *size = 0;
    if (store == NULL || store->load == NULL || !domain_valid(domain) ||
        (capacity != 0 && buffer == NULL)) {
        return GS232B_ERROR_INVALID_ARGUMENT;
    }
    gs232b_error_t error = store->load(store->ctx, domain, buffer, capacity, size);
    if (error != GS232B_OK && error != GS232B_ERROR_BUFFER_TOO_SMALL) {
        *size = 0;
    } else if ((error == GS232B_OK && *size > capacity) ||
               (error == GS232B_ERROR_BUFFER_TOO_SMALL && *size <= capacity)) {
        *size = 0;
        return GS232B_ERROR_IO;
    }
    return error;
}

gs232b_error_t gs232b_store_save(const gs232b_store_t *store,
                                gs232b_store_domain_t domain,
                                const uint8_t *record, size_t size)
{
    if (store == NULL || store->save == NULL || !domain_valid(domain) ||
        (size != 0 && record == NULL)) {
        return GS232B_ERROR_INVALID_ARGUMENT;
    }
    return store->save(store->ctx, domain, record, size);
}

gs232b_error_t gs232b_store_erase(const gs232b_store_t *store,
                                 gs232b_store_domain_t domain)
{
    if (store == NULL || store->erase == NULL || !domain_valid(domain)) {
        return GS232B_ERROR_INVALID_ARGUMENT;
    }
    return store->erase(store->ctx, domain);
}

gs232b_error_t gs232b_session_receive(const gs232b_session_io_t *io,
                                     gs232b_session_key_t session,
                                     uint8_t *buffer, size_t capacity, size_t *size)
{
    if (size == NULL) {
        return GS232B_ERROR_INVALID_ARGUMENT;
    }
    *size = 0;
    if (io == NULL || io->receive == NULL || !gs232b_session_key_valid(session) ||
        buffer == NULL || capacity == 0) {
        return GS232B_ERROR_INVALID_ARGUMENT;
    }
    gs232b_error_t error = io->receive(io->ctx, session, buffer, capacity, size);
    if (error != GS232B_OK) {
        *size = 0;
    } else if (*size == 0) {
        return GS232B_ERROR_WOULD_BLOCK;
    } else if (*size > capacity) {
        *size = 0;
        return GS232B_ERROR_IO;
    }
    return error;
}

gs232b_error_t gs232b_session_send(const gs232b_session_io_t *io,
                                  gs232b_session_key_t session,
                                  const uint8_t *bytes, size_t size)
{
    if (io == NULL || io->send == NULL || !gs232b_session_key_valid(session) ||
        (size != 0 && bytes == NULL)) {
        return GS232B_ERROR_INVALID_ARGUMENT;
    }
    return io->send(io->ctx, session, bytes, size);
}

gs232b_error_t gs232b_session_close(const gs232b_session_io_t *io,
                                   gs232b_session_key_t session)
{
    if (io == NULL || io->close == NULL || !gs232b_session_key_valid(session)) {
        return GS232B_ERROR_INVALID_ARGUMENT;
    }
    return io->close(io->ctx, session);
}
