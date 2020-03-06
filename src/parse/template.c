#include "gwion_util.h"
#include "gwion_ast.h"
#include "gwion_env.h"
#include "vm.h"
#include "traverse.h"
#include "template.h"
#include "vm.h"
#include "parse.h"
#include "gwion.h"
#include "operator.h"

struct tmpl_info {
  const  Class_Def cdef;
  Type_List        call;
  struct Vector_   type;
  struct Vector_   size;
  uint8_t index;
};

ANN m_bool template_push_types(const Env env, const Tmpl *tmpl) {
  ID_List list = tmpl->list;
  Type_List call = tmpl->call;
  nspc_push_type(env->gwion->mp, env->curr);
  do {
    if(!call)
      break;
    const Type t = known_type(env, call->td);
    if(!t)
      return GW_ERROR;
    nspc_add_type(env->curr, list->xid, t);
    call = call->next;
  } while((list = list->next));
  if(!call)
    return GW_OK;
  POP_RET(-1);
}

ANN Tmpl* mk_tmpl(const Env env, const Tmpl *tm, const Type_List types) {
  Tmpl *tmpl = new_tmpl(env->gwion->mp, tm->list, 0);
  tmpl->call = cpy_type_list(env->gwion->mp, types);
  return tmpl;
}

static ANN Type scan_func(const Env env, const Type t, const Type_Decl* td) {
  DECL_OO(const m_str, tl_name, = tl2str(env, td->types))
  const Symbol sym = func_symbol(env, t->e->owner->name, t->e->d.func->name, tl_name, 0);
  free_mstr(env->gwion->mp, tl_name);
  const Type base_type = nspc_lookup_type1(t->e->owner, sym);
  if(base_type)
    return base_type;
  const Type ret = type_copy(env->gwion->mp, t);
  ADD_REF(ret->nspc)
  ret->e->parent = t;
  ret->name = s_name(sym);
  SET_FLAG(ret, func);
  nspc_add_type_front(t->e->owner, sym, ret);
  const Func_Def def = cpy_func_def(env->gwion->mp, t->e->d.func->def);
  const Func func = ret->e->d.func = new_func(env->gwion->mp, s_name(sym), def);
  const Value value = new_value(env->gwion->mp, ret, s_name(sym));
  func->flag = def->flag;
  value->d.func_ref = func;
  value->from->owner = t->e->owner;
  value->from->owner_class = t->e->d.func->value_ref->from->owner_class;
  func->value_ref = value;
  func->def->base->tmpl = mk_tmpl(env, t->e->d.func->def->base->tmpl, td->types);
  def->base->func = func;
  nspc_add_value_front(t->e->owner, sym, value);
  return ret;
}

static ANN Type maybe_func(const Env env, const Type t, const Type_Decl* td) {
  if(isa(t, env->gwion->type[et_function]) > 0 && t->e->d.func->def->base->tmpl)
     return scan_func(env, t, td);
   ERR_O(td->xid->pos,
      _("type '%s' is not template. You should not provide template types"), t->name)
}

ANN Type scan_type(const Env env, const Type t, const Type_Decl* td) {
  if(GET_FLAG(t, template)) {
    if(GET_FLAG(t, ref))
      return t;
    struct TemplateScan ts = { .t=t, .td=td };
    struct Op_Import opi = { .op=insert_symbol("@scan"), .lhs=t, .data=(uintptr_t)&ts, .pos=td_pos(td) };
    return op_check(env, &opi);
  } else if(td->types)
    return maybe_func(env, t, td);
  return t;
}
