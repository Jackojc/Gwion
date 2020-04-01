#include "gwion_util.h"
#include "gwion_ast.h"
#include "gwion_env.h"
#include "vm.h"
#include "traverse.h"
#include "parse.h"
#include "gwion.h"
#include "tuple.h"

ANN static inline m_bool freeable(const Type a) {
  return !GET_FLAG(a, nonnull) &&
    (GET_FLAG(a, template) || GET_FLAG(a, global));
}

ANN static void free_type(Type a, Gwion gwion) {
  if(freeable(a)) {
    if(GET_FLAG(a, union)) {
      if(a->e->def->union_def) {
        if(!GET_FLAG(a, pure))  { // <=> decl_list
          UNSET_FLAG(a->e->def->union_def, global);
          free_union_def(gwion->mp, a->e->def->union_def);
        } else
          free_decl_list(gwion->mp, a->e->def->list);
      }
      a->e->def->union_def = NULL;
    } else if(a->e->def) {
      if(!(a->e->ctx && a->e->ctx->error)) {
        UNSET_FLAG(a->e->def, template);
        UNSET_FLAG(a->e->def, global);
        free_class_def(gwion->mp, a->e->def);
      }
    }
  }
  if(a->nspc)
    REM_REF(a->nspc, gwion);
  if(a->e->tuple)
    free_tupleform(gwion->mp, a->e->tuple);
  if(a->e->contains.ptr) {
    for(m_uint i = 0; i < vector_size(&a->e->contains); ++i)
      REM_REF((Type)vector_at(&a->e->contains, i), gwion);
    vector_release(&a->e->contains);
  }
  mp_free(gwion->mp, TypeInfo, a->e);
  mp_free(gwion->mp, Type, a);
}

Type new_type(MemPool p, const m_uint xid, const m_str name, const Type parent) {
  const Type type = mp_calloc(p, Type);
  type->xid    = xid;
  type->name   = name;
  type->e = mp_calloc(p, TypeInfo);
  type->e->parent = parent;
  if(parent)
    type->size = parent->size;
  type->ref = new_refcount(p, free_type);
  return type;
}

ANN Type type_copy(MemPool p, const Type type) {
  const Type a = new_type(p, type->xid, type->name, type->e->parent);
  a->nspc          = type->nspc;
  a->e->owner         = type->e->owner;
  a->size          = type->size;
  a->e->d.base_type   = type->e->d.base_type;
  a->array_depth   = type->array_depth;
  a->e->def           = type->e->def;
  a->e->gack           = type->e->gack;
  return a;
}

ANN m_bool isa(const restrict Type var, const restrict Type parent) {
  return (var->xid == parent->xid) ? 1 : var->e->parent ? isa(var->e->parent, parent) : -1;
}

ANN Type find_common_anc(const restrict Type lhs, const restrict Type rhs) {
  return isa(lhs, rhs) > 0 ? rhs : isa(rhs, lhs) > 0 ? lhs : NULL;
}

#define describe_find(name, t)                                       \
ANN t find_##name(const Type type, const Symbol xid) {               \
  if(type->nspc) {                                                   \
  const t val = nspc_lookup_##name##2(type->nspc, xid);              \
  if(val)                                                            \
    return val;                                                      \
  }                                                                  \
  return type->e->parent ? find_##name(type->e->parent, xid) : NULL; \
}
describe_find(value, Value)
//describe_find(func,  Func)

ANN Type typedef_base(Type t) {
  while(GET_FLAG(t, typedef))
    t = t->e->parent;
  return t;
}

ANN Type array_base(Type type) {
  const Type t = typedef_base(type);
  return t->e->d.base_type;
}

ANN static Symbol array_sym(const Env env, const Type src, const m_uint depth) {
  size_t len = strlen(src->name);
  char name[len + 2* depth + 1];
  strcpy(name, src->name);
  m_uint i = depth + 1;
  while(--i) {
    strcpy(name+len, "[]");
    len += 2;
  }
  return insert_symbol(name);
}

ANN Type array_type(const Env env, const Type src, const m_uint depth) {
  const Symbol sym = array_sym(env, src, depth);
  const Type type = nspc_lookup_type1(src->e->owner, sym);
  if(type)
    return type;
  const Type t = new_type(env->gwion->mp, env->gwion->type[et_array]->xid,
      s_name(sym), env->gwion->type[et_array]);
  t->array_depth = depth + src->array_depth;
  t->e->d.base_type = array_base(src) ?: src;
  t->e->owner = src->e->owner;
  ADD_REF((t->nspc = env->gwion->type[et_array]->nspc))
  SET_FLAG(t, checked);
  mk_class(env, t); // maybe add_type_front could go in mk_class ?
  nspc_add_type_front(src->e->owner, sym, t);
  return t;
}

ANN m_bool type_ref(Type t) {
  do {
    if(GET_FLAG(t, empty))
      return GW_OK;
    if(GET_FLAG(t, typedef) && t->e->def)
      if(t->e->def->base.ext && t->e->def->base.ext->array) {
        if(!t->e->def->base.ext->array->exp)
          return GW_OK;
        else {
          const Type type = t->e->parent->e->d.base_type;
          if(SAFE_FLAG(type, empty))
            return GW_OK;
        }
      }
  } while((t = t->e->parent));
  return 0;
}

ANN m_str get_type_name(const Env env, const m_str s, const m_uint index) {
  m_str name = strstr(s, "<");
  m_uint i = 0;
  m_uint lvl = 0;
  m_uint n = 1;
  const size_t len = name ? strlen(name) : 0;
  const size_t slen = strlen(s);
  const size_t tlen = slen - len + 1;
  char c[slen + 1];
  if(!name)
    return index ? NULL : s_name(insert_symbol(s));
  if(index == 0) {
    snprintf(c, tlen, "%s", s);
    return s_name(insert_symbol(c));
  }
  ++name;
  while(*name++) {
    if(*name == '<') {
      if(n == index)
        c[i++] = *name;
      lvl++;
      name++;
    } else if(*name == '~' && !lvl--)
      break;
    if(n == index)
      c[i++] = *name;
  }
  c[i] = '\0';
  return strlen(c) ? s_name(insert_symbol(c)) : NULL;
}

ANN m_uint get_depth(const Type type) {
  m_uint depth = 0;
  Type t = type;
  do {
    if(t->array_depth) {
      depth += t->array_depth;
      t = t->e->d.base_type;
    } else
      t = t->e->parent;
  } while(t);
  return depth;
}

ANN m_bool is_fptr(const struct Gwion_* gwion, const Type t) {
  return isa(actual_type(gwion, t), gwion->type[et_fptr]) > 0;
}
ANN inline m_bool is_class(const struct Gwion_* gwion, const Type t) {
  return isa(t, gwion->type[et_class]) > 0;
}

ANN Type actual_type(const struct Gwion_* gwion, const Type t) {
  return is_class(gwion, t) ? t->e->d.base_type : t;
}
