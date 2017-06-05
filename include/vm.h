#ifndef __VM
#define __VM

#include "defs.h"
#include "map.h"

typedef struct VM_Code_* VM_Code;
typedef enum { NATIVE_UNKNOWN, NATIVE_CTOR, NATIVE_DTOR, NATIVE_MFUN, NATIVE_SFUN } e_native_func;
struct VM_Code_ {
  Vector instr;
  m_str name, filename;
  m_uint stack_depth;
  m_uint native_func;
  e_native_func native_func_type;
  m_bool need_this;
};

typedef struct BBQ_* BBQ;
typedef struct Shreduler_* Shreduler;
typedef struct {
  Vector shred, ugen;
  BBQ bbq;
  Shreduler shreduler;
  M_Object adc, dac, blackhole;
  Emitter emit;
  Env env;
  void (*wakeup)();
  Vector plug;
  m_bool is_running;
} VM;

typedef struct VM_Shred_* VM_Shred;
struct VM_Shred_ {
  VM_Code code;
  VM_Shred parent;
  char* reg;
  char* mem;
  char* _reg;
  char* _mem;
  char* base;
  m_uint pc, next_pc, xid;
  m_str name;
  VM* vm_ref;
  m_bool is_running, is_done;
  VM_Shred prev, next;
  Vector args;
  M_Object me;
  m_str filename;
  Vector child;
  M_Object wait;
  Vector gc;
  Vector gc1;
#ifdef DEBUG_STACK
  m_int mem_index, reg_index;
#endif
  m_float wake_time;
};

VM_Code new_vm_code(Vector instr, m_uint stack_depth, m_bool need_this, m_str name, m_str filename);
void free_vm_code(VM_Code a);

Shreduler new_shreduler(VM* vm);
VM_Shred shreduler_get(Shreduler s);
m_bool shreduler_remove(Shreduler s, VM_Shred out, m_bool erase);

VM_Shred new_vm_shred(VM_Code code);
void free_vm_shred(VM_Shred shred);

void vm_run(VM* vm);
VM* new_vm(m_bool loop);
void free_vm(VM* vm);
void vm_add_shred(VM* vm, VM_Shred shred);
#endif
