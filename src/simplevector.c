// This file is a part of Julia. License is MIT: https://julialang.org/license

#include <stdlib.h>
#include <string.h>
#include <stdarg.h>
#include "julia.h"
#include "julia_internal.h"
#include "julia_assert.h"

JL_DLLEXPORT jl_svec_t *(ijl_svec)(size_t n, ...)
{
    va_list args;
    if (n == 0) return jl_emptysvec;
    va_start(args, n);
    jl_svec_t *jv = jl_alloc_svec_uninit(n);
    for(size_t i=0; i < n; i++)
        jl_svecset(jv, i, va_arg(args, jl_value_t*));
    va_end(args);
    return jv;
}

jl_svec_t *(jl_perm_symsvec)(size_t n, ...)
{
    if (n == 0) return jl_emptysvec;
    jl_svec_t *jv = (jl_svec_t*)jl_gc_permobj((n + 1) * sizeof(void*), jl_simplevector_type);
    jl_svec_set_len_unsafe(jv, n);
    va_list args;
    va_start(args, n);
    for (size_t i = 0; i < n; i++)
        jl_svecset(jv, i, jl_symbol(va_arg(args, const char*)));
    va_end(args);
    return jv;
}

JL_DLLEXPORT jl_svec_t *jl_svec1(void *a)
{
    jl_task_t *ct = jl_current_task;
    jl_svec_t *v = (jl_svec_t*)jl_gc_alloc(ct->ptls, sizeof(void*) * 2,
                                           jl_simplevector_type);
    jl_svec_set_len_unsafe(v, 1);
    jl_svecset(v, 0, a);
    return v;
}

JL_DLLEXPORT jl_svec_t *jl_svec2(void *a, void *b)
{
    jl_task_t *ct = jl_current_task;
    jl_svec_t *v = (jl_svec_t*)jl_gc_alloc(ct->ptls, sizeof(void*) * 3,
                                           jl_simplevector_type);
    jl_svec_set_len_unsafe(v, 2);
    jl_svecset(v, 0, a);
    jl_svecset(v, 1, b);
    return v;
}

JL_DLLEXPORT jl_svec_t *jl_alloc_svec_uninit(size_t n)
{
    jl_task_t *ct = jl_current_task;
    if (n == 0) return jl_emptysvec;
    jl_svec_t *jv = (jl_svec_t*)jl_gc_alloc(ct->ptls, (n + 1) * sizeof(void*),
                                            jl_simplevector_type);
    jl_svec_set_len_unsafe(jv, n);
    return jv;
}

JL_DLLEXPORT jl_svec_t *jl_alloc_svec(size_t n)
{
    if (n == 0) return jl_emptysvec;
    jl_svec_t *jv = jl_alloc_svec_uninit(n);
    for(size_t i=0; i < n; i++)
        jl_svecset(jv, i, NULL);
    return jv;
}

JL_DLLEXPORT jl_svec_t *jl_svec_copy(jl_svec_t *a)
{
    size_t i, n=jl_svec_len(a);
    jl_svec_t *c = jl_alloc_svec_uninit(n);
    for(i=0; i < n; i++)
        jl_svecset(c, i, jl_svecref(a,i));
    return c;
}

JL_DLLEXPORT jl_svec_t *jl_svec_fill(size_t n, jl_value_t *x)
{
    if (n==0) return jl_emptysvec;
    jl_svec_t *v = jl_alloc_svec_uninit(n);
    for(size_t i=0; i < n; i++)
        jl_svecset(v, i, x);
    return v;
}

JL_DLLEXPORT size_t (jl_svec_len)(jl_svec_t *t) JL_NOTSAFEPOINT
{
    return jl_svec_len(t);
}

JL_DLLEXPORT int8_t jl_svec_isassigned(jl_svec_t *t JL_PROPAGATES_ROOT, ssize_t i) JL_NOTSAFEPOINT
{
    return jl_svecref(t, (size_t)i) != NULL;
}

JL_DLLEXPORT jl_value_t *jl_svec_ref(jl_svec_t *t JL_PROPAGATES_ROOT, ssize_t i)
{
    jl_value_t *v = jl_svecref(t, (size_t)i);
    if (__unlikely(v == NULL))
        jl_throw(jl_undefref_exception);
    return v;
}
