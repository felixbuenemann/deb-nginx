#define DDEBUG 0

#include "ddebug.h"

#include "ngx_http_headers_more_util.h"
#include <ctype.h>

ngx_int_t
ngx_http_headers_more_parse_header(ngx_conf_t *cf, ngx_str_t *cmd_name,
        ngx_str_t *raw_header, ngx_array_t *headers,
        ngx_http_headers_more_opcode_t opcode,
        ngx_http_headers_more_set_header_t *handlers)
{
    ngx_http_headers_more_header_val_t             *hv;

    ngx_uint_t                        i;
    ngx_str_t                         key = ngx_null_string;
    ngx_str_t                         value = ngx_null_string;
    ngx_flag_t                        seen_end_of_key;
    ngx_http_compile_complex_value_t  ccv;

    hv = ngx_array_push(headers);
    if (hv == NULL) {
        return NGX_ERROR;
    }

    seen_end_of_key = 0;
    for (i = 0; i < raw_header->len; i++) {
        if (key.len == 0) {
            if (isspace(raw_header->data[i])) {
                continue;
            }

            key.data = raw_header->data;
            key.len  = 1;

            continue;
        }

        if (!seen_end_of_key) {
            if (raw_header->data[i] == ':'
                    || isspace(raw_header->data[i])) {
                seen_end_of_key = 1;
                continue;
            }

            key.len++;

            continue;
        }

        if (value.len == 0) {
            if (raw_header->data[i] == ':'
                    || isspace(raw_header->data[i])) {
                continue;
            }

            value.data = &raw_header->data[i];
            value.len  = 1;

            continue;
        }

        value.len++;
    }

    if (key.len == 0) {
        ngx_log_error(NGX_LOG_ERR, cf->log, 0,
              "%V: no key found in the header argument: %V",
              cmd_name, raw_header);

        return NGX_ERROR;
    }

    hv->wildcard = (key.data[key.len-1] == '*');
    if (hv->wildcard && key.len<2){
      ngx_log_error(NGX_LOG_ERR, cf->log, 0,
                    "%V: wildcard key to short: %V",
                    cmd_name, raw_header);
      return NGX_ERROR;
    }

    hv->hash = 1;
    hv->key = key;

    hv->offset = 0;

    for (i = 0; handlers[i].name.len; i++) {
        if (hv->key.len != handlers[i].name.len
                || ngx_strncasecmp(hv->key.data, handlers[i].name.data,
                    handlers[i].name.len) != 0)
        {
            dd("hv key comparison: %s <> %s", handlers[i].name.data,
                    hv->key.data);

            continue;
        }

        hv->offset = handlers[i].offset;
        hv->handler = handlers[i].handler;

        break;
    }

    if (handlers[i].name.len == 0 && handlers[i].handler) {
        hv->offset = handlers[i].offset;
        hv->handler = handlers[i].handler;
    }

    if (opcode == ngx_http_headers_more_opcode_clear) {
        value.len = 0;
    }

    if (value.len == 0) {
        ngx_memzero(&hv->value, sizeof(ngx_http_complex_value_t));
        return NGX_OK;
    }

    ngx_memzero(&ccv, sizeof(ngx_http_compile_complex_value_t));

    ccv.cf = cf;
    ccv.value = &value;
    ccv.complex_value = &hv->value;

    if (ngx_http_compile_complex_value(&ccv) != NGX_OK) {
        return NGX_ERROR;
    }

    return NGX_OK;
}


ngx_int_t
ngx_http_headers_more_parse_statuses(ngx_log_t *log, ngx_str_t *cmd_name,
    ngx_str_t *value, ngx_array_t *statuses)
{
    u_char          *p, *last;
    ngx_uint_t      *s = NULL;

    p = value->data;
    last = p + value->len;

    for (; p != last; p++) {
        if (s == NULL) {
            if (isspace(*p)) {
                continue;
            }

            s = ngx_array_push(statuses);
            if (s == NULL) {
                return NGX_ERROR;
            }

            if (*p >= '0' && *p <= '9') {
                *s = *p - '0';
            } else {
                ngx_log_error(NGX_LOG_ERR, log, 0,
                      "%V: invalid digit \"%c\" found in "
                      "the status code list \"%V\"",
                      cmd_name, *p, value);

                return NGX_ERROR;
            }

            continue;
        }

        if (isspace(*p)) {
            dd("Parsed status %d", (int) *s);

            s = NULL;
            continue;
        }

        if (*p >= '0' && *p <= '9') {
            *s *= 10;
            *s += *p - '0';
        } else {
            ngx_log_error(NGX_LOG_ERR, log, 0,
                  "%V: invalid digit \"%c\" found in "
                  "the status code list \"%V\"",
                  cmd_name, *p, value);

            return NGX_ERROR;
        }
    }

    if (s) {
        dd("Parsed status %d", (int) *s);
    }

    return NGX_OK;
}


ngx_int_t
ngx_http_headers_more_parse_types(ngx_log_t *log, ngx_str_t *cmd_name,
    ngx_str_t *value, ngx_array_t *types)
{
    u_char          *p, *last;
    ngx_str_t       *t = NULL;

    p = value->data;
    last = p + value->len;

    for (; p != last; p++) {
        if (t == NULL) {
            if (isspace(*p)) {
                continue;
            }

            t = ngx_array_push(types);
            if (t == NULL) {
                return NGX_ERROR;
            }

            t->len = 1;
            t->data = p;

            continue;
        }

        if (isspace(*p)) {
            t = NULL;
            continue;
        }

        t->len++;
    }

    return NGX_OK;
}


ngx_int_t
ngx_http_headers_more_rm_header(ngx_list_t *l, ngx_table_elt_t *h)
{
    ngx_uint_t                   i;
    ngx_list_part_t             *part;
    ngx_table_elt_t             *data;

    part = &l->part;
    data = part->elts;

    for (i = 0; /* void */; i++) {
        dd("i: %d, part: %p", (int) i, part);

        if (i >= part->nelts) {
            if (part->next == NULL) {
                break;
            }

            part = part->next;
            h = part->elts;
            i = 0;
        }

        if (&data[i] == h) {
            dd("found header");

            return ngx_http_headers_more_rm_header_helper(l, part, i);
        }
    }

    return NGX_ERROR;
}


ngx_int_t
ngx_http_headers_more_rm_header_helper(ngx_list_t *l, ngx_list_part_t *cur,
        ngx_uint_t i)
{
    ngx_table_elt_t             *data;
    ngx_list_part_t             *new, *part;

    dd("list rm item: part %p, i %d, nalloc %d", cur, (int) i,
            (int) l->nalloc);

    data = cur->elts;

    dd("cur: nelts %d, nalloc %d", (int) cur->nelts,
            (int) l->nalloc);

    if (i == 0) {
        cur->elts = (char *) cur->elts + l->size;
        cur->nelts--;

        if (cur == l->last) {
            if (l->nalloc > 1) {
                l->nalloc--;
                return NGX_OK;
            }

            /* l->nalloc == 1 */

            part = &l->part;
            while (part->next != cur) {
                if (part->next == NULL) {
                    return NGX_ERROR;
                }
                part = part->next;
            }

            part->next = NULL;
            l->last = part;

            return NGX_OK;
        }

        if (cur->nelts == 0) {
            part = &l->part;
            while (part->next != cur) {
                if (part->next == NULL) {
                    return NGX_ERROR;
                }
                part = part->next;
            }

            part->next = cur->next;

            return NGX_OK;
        }

        return NGX_OK;
    }

    if (i == cur->nelts - 1) {
        cur->nelts--;

        if (cur == l->last) {
            l->nalloc--;
        }

        return NGX_OK;
    }

    new = ngx_palloc(l->pool, sizeof(ngx_list_part_t));
    if (new == NULL) {
        return NGX_ERROR;
    }

    new->elts = &data[i + 1];
    new->nelts = cur->nelts - i - 1;
    new->next = cur->next;

    l->nalloc = new->nelts;

    cur->nelts = i;
    cur->next = new;
    if (cur == l->last) {
        l->last = new;
    }

    cur = new;

    return NGX_OK;
}

