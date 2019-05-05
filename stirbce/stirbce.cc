#include <stdio.h>
#include <stdint.h>
#include <stddef.h>
#include <exception>
#include <iostream>
#include "opcodes.h"
#include "engine.h"
#include "errno.h"
#include "stirbce.h"

#define likely(x)       __builtin_expect((x),1)
#define unlikely(x)     __builtin_expect((x),0)

#if 0
static int store_u64(uint64_t *s, uint64_t sp, uint8_t *v, uint64_t vsz, uint64_t loc, uint64_t val)
{
  if ((~loc) < loc)
  {
    loc = ~loc;
    if (unlikely(loc >= sp))
    {
      return 0;
    }
    s[sp - loc - 1] = val;
    return 1;
  }
  if (loc > (vsz-8)/8)
  {
    return 0;
  }
  v[loc*8+0] = val>>56;
  v[loc*8+2] = val>>48;
  v[loc*8+3] = val>>40;
  v[loc*8+3] = val>>32;
  v[loc*8+4] = val>>24;
  v[loc*8+5] = val>>16;
  v[loc*8+6] = val>>8;
  v[loc*8+7] = val;
  return 1;
}
static uint64_t load_u64(uint64_t *s, uint64_t sp, const uint8_t *v, uint64_t vsz, uint64_t loc)
{
  if ((~loc) < loc)
  {
    loc = ~loc;
    if (unlikely(loc >= sp))
    {
      return 0;
    }
    return s[sp - loc - 1];
  }
  if (loc > (vsz-8)/8)
  {
    return 0;
  }
  return (((uint64_t)v[loc*8+0])<<56) |
         (((uint64_t)v[loc*8+1])<<48) |
         (((uint64_t)v[loc*8+2])<<40) |
         (((uint64_t)v[loc*8+3])<<32) |
         (((uint64_t)v[loc*8+4])<<24) |
         (((uint64_t)v[loc*8+5])<<16) |
         (((uint64_t)v[loc*8+6])<<8) |
         v[loc*8 + 7];
}
#endif

double get_dbl(std::vector<memblock> &stack)
{
  memblock mb = stack.back();
  stack.pop_back();
  if (mb.type != memblock::T_D && mb.type != memblock::T_B)
  {
    std::terminate();
  }
  return mb.u.d;
}
int64_t get_i64(std::vector<memblock> &stack)
{
  return get_dbl(stack);
}

memblock &
get_stackloc(int64_t stackloc, size_t bp, std::vector<memblock> &stack)
{
  if (stackloc >= 0)
  {
    return stack.at(bp + stackloc);
  }
  return stack.at(stack.size() + stackloc);
}

int engine(const uint8_t *microprogram, size_t microsz,
           stringtab &st, lua_State *lua)
{
  // 0.53 us / execution
  size_t ip = 0;
  int ret = 0;
  int64_t val, val2, condition, jmp;
  size_t bp;
  const size_t stackbound = 131072;
  std::vector<memblock> stack;

  ip = 0;
  ret = -EAGAIN;
  while (ret == -EAGAIN && ip < microsz)
  {
    uint8_t instr = microprogram[ip++];
    printf("ip %zu instr %d\n", ip-1, (int)instr);
    switch (instr)
    {
      case STIRBCE_OPCODE_JMP:
        if (unlikely(stack.size() < 1))
        {
          printf("stack underflow\n");
          ret = -EOVERFLOW;
          break;
        }
        jmp = get_i64(stack);
        if (unlikely(ip + jmp > microsz))
        {
          printf("microprogram overflow\n");
          ret = -EFAULT;
          break;
        }
        ip += jmp;
        break;
      case STIRBCE_OPCODE_LUAPUSH:
      {
        if (unlikely(stack.size() < 1))
        {
          printf("stack underflow\n");
          ret = -EOVERFLOW;
          break;
        }
        stack[stack.size() - 1].push_lua(lua);
        break;
      }
      case STIRBCE_OPCODE_LUAEVAL:
      {
        int top = lua_gettop(lua);
        if (unlikely(stack.size() < 1))
        {
          printf("stack underflow\n");
          ret = -EOVERFLOW;
          break;
        }
        memblock mb = stack.back();
        stack.pop_back();
        if (mb.type != memblock::T_S)
        {
          printf("not a string\n");
          ret = -EINVAL;
          break;
        }
        luaL_dostring(lua, mb.u.s->c_str());
        if (lua_gettop(lua) == top)
        {
          lua_pushnil(lua);
        }
        else if (lua_gettop(lua) != top + 1)
        {
          printf("lua multi-return not supported\n");
          ret = -EINVAL;
          break;
        }
        stack.push_back(lua);
        break;
      }
      case STIRBCE_OPCODE_CALL:
      {
        int64_t argcnt;
        if (unlikely(stack.size() < 2))
        {
          printf("stack underflow\n");
          ret = -EOVERFLOW;
          break;
        }
        argcnt = get_i64(stack);
        if (argcnt < 0)
        {
          printf("negative argument count\n");
          ret = -EINVAL;
          break;
        }
        jmp = get_i64(stack);
        if (jmp < 0 || (size_t)jmp + 9 > microsz) // FIXME off-by-one?
        {
          printf("microprogram overflow\n");
          ret = -EFAULT;
          break;
        }
        printf("call, stack size %zu jmp %zu usz %zu\n", stack.size(), jmp, microsz);
        printf("instr %d\n", microprogram[jmp]);
        if (microprogram[jmp] != STIRBCE_OPCODE_FUN_HEADER)
        {
          printf("not a function\n");
          ret = -EINVAL;
          break;
        }
        uint64_t u64 = 
                      ((((uint64_t)microprogram[jmp+1])<<56) |
                      (((uint64_t)microprogram[jmp+2])<<48) |
                      (((uint64_t)microprogram[jmp+3])<<40) |
                      (((uint64_t)microprogram[jmp+4])<<32) |
                      (((uint64_t)microprogram[jmp+5])<<24) |
                      (((uint64_t)microprogram[jmp+6])<<16) |
                      (((uint64_t)microprogram[jmp+7])<<8) |
                                  microprogram[jmp+8]);
        double dbl;
        memcpy(&dbl, &u64, 8);
        if (dbl != (double)argcnt)
        {
          printf("function argument count not correct %g %g\n", dbl, (double)argcnt);
          ret = -EINVAL;
          break;
        }
        stack.push_back(ip);
        ip = jmp + 9;
        break;
      }
      case STIRBCE_OPCODE_EXIT:
      {
        printf("exit, stack size %zu\n", stack.size());
        ip = microsz;
        break;
      }
      case STIRBCE_OPCODE_DUMP:
      {
        printf("dump, stack size %zu\n", stack.size());
        if (unlikely(stack.size() < 1))
        {
          printf("stack underflow\n");
          ret = -EOVERFLOW;
          break;
        }
        memblock mb = stack.back();
        stack.pop_back();
        mb.dump();
        std::cout << std::endl;
        break;
      }
      case STIRBCE_OPCODE_RETEX:
      {
        printf("retex1, stack size %zu\n", stack.size());
        if (unlikely(stack.size() < 2))
        {
          printf("stack underflow\n");
          ret = -EOVERFLOW;
          break;
        }
        int64_t cnt = get_i64(stack); // Count of local vars
        if (cnt < 0 || (size_t)cnt+2 >= stack.size())
        {
          printf("stack underflow\n");
          ret = -EOVERFLOW;
          break;
        }
        memblock mb = stack.back();
        stack.pop_back();
        for (int i = 0; i < cnt; i++)
        {
          stack.pop_back(); // Clear local variable block
        }
        jmp = get_i64(stack);
        if (unlikely(jmp < 0 || (size_t)jmp > microsz))
        {
          printf("microprogram overflow\n");
          ret = -EFAULT;
          break;
        }
        printf("retex, stack size %zu w/o retval\n", stack.size());
        if (mb.type == memblock::T_V)
        {
          std::cout << "[ ";
          for (auto it = mb.u.v->begin(); it != mb.u.v->end(); it++)
          {
            memblock mb2 = *it;
            if (mb2.type == memblock::T_V)
            {
              std::cout << "[ ";
              for (auto it2 = mb2.u.v->begin(); it2 != mb2.u.v->end(); it2++)
              {
                memblock mb3 = *it2;
                if (mb3.type == memblock::T_S)
                {
                  std::cout << *mb3.u.s << ", ";
                }
                else
                {
                  std::cout << "UNKNOWN, ";
                }
              }
              std::cout << "], ";
            }
            else
            {
              std::cout << "UNKNOWN, ";
            }
          }
          std::cout << "];";
          std::cout << std::endl;
        }
        else
        {
          std::cout << "UNKNOWN: " << mb.type << std::endl;
        }
        ip = jmp;
        stack.push_back(mb);
        break;
      }
      case STIRBCE_OPCODE_RET:
      {
        printf("ret1, stack size %zu\n", stack.size());
        if (unlikely(stack.size() < 2))
        {
          printf("stack underflow\n");
          ret = -EOVERFLOW;
          break;
        }
        memblock mb = stack.back();
        stack.pop_back();
        jmp = get_i64(stack);
        if (unlikely(jmp < 0 || (size_t)jmp > microsz))
        {
          printf("microprogram overflow\n");
          ret = -EFAULT;
          break;
        }
        printf("ret, stack size %zu w/o retval\n", stack.size());
        if (mb.type == memblock::T_V)
        {
          std::cout << "[ ";
          for (auto it = mb.u.v->begin(); it != mb.u.v->end(); it++)
          {
            memblock mb2 = *it;
            if (mb2.type == memblock::T_V)
            {
              std::cout << "[ ";
              for (auto it2 = mb2.u.v->begin(); it2 != mb2.u.v->end(); it2++)
              {
                memblock mb3 = *it2;
                if (mb3.type == memblock::T_S)
                {
                  std::cout << *mb3.u.s << ", ";
                }
                else
                {
                  std::cout << "UNKNOWN, ";
                }
              }
              std::cout << "], ";
            }
            else
            {
              std::cout << "UNKNOWN, ";
            }
          }
          std::cout << "];";
          std::cout << std::endl;
        }
        else
        {
          std::cout << "UNKNOWN: " << mb.type << std::endl;
        }
        ip = jmp;
        stack.push_back(mb);
        break;
      }
      case STIRBCE_OPCODE_IF_NOT_JMP:
        if (unlikely(stack.size() < 2))
        {
          printf("stack underflow\n");
          ret = -EOVERFLOW;
          break;
        }
        jmp = get_i64(stack);
        condition = get_i64(stack);
        if (unlikely(ip + jmp > microsz))
        {
          printf("microprogram overflow\n");
          ret = -EFAULT;
          break;
        }
        if (!condition)
        {
          ip += jmp;
        }
        break;
      case STIRBCE_OPCODE_NOP:
        break;
      case STIRBCE_OPCODE_POP:
        if (unlikely(stack.size() <= 0))
        {
          printf("stack underflow\n");
          ret = -EOVERFLOW;
          break;
        }
        stack.pop_back();
        break;
      case STIRBCE_OPCODE_POP_MANY:
        if (unlikely(stack.size() <= 0))
        {
          printf("stack underflow\n");
          ret = -EOVERFLOW;
          break;
        }
        val = get_i64(stack);
        if (val < 0 || unlikely(stack.size() < (size_t)val))
        {
          printf("stack underflow\n");
          ret = -EOVERFLOW;
          break;
        }
        stack.resize(stack.size() - val);
        break;
      case STIRBCE_OPCODE_PUSH_STACK:
      {
        if (unlikely(stack.size() <= 0))
        {
          printf("stack underflow\n");
          ret = -EOVERFLOW;
          break;
        }
        val = get_i64(stack);
        if (val < 0 || (size_t)val >= stack.size())
        {
          printf("stack underflow\n");
          ret = -EOVERFLOW;
          break;
        }
        memblock mb = stack[stack.size() - val - 1];
        stack.push_back(mb);
        break;
      }
      case STIRBCE_OPCODE_SET_STACK:
      {
        if (unlikely(stack.size() <= 0))
        {
          printf("stack underflow\n");
          ret = -EOVERFLOW;
          break;
        }
        memblock mb = stack.back();
        stack.pop_back();
        val = get_i64(stack);
        if (val < 0 || (size_t)val >= stack.size())
        {
          printf("stack underflow\n");
          ret = -EOVERFLOW;
          break;
        }
        stack[stack.size() - val - 1] = mb;
        break;
      }
      case STIRBCE_OPCODE_PUSH_NEW_ARRAY:
      {
        if (unlikely(stack.size() >= stackbound))
        {
          printf("stack overflow\n");
          ret = -EOVERFLOW;
          break;
        }
        stack.push_back(new std::vector<memblock>());
        break;
      }
      case STIRBCE_OPCODE_PUSH_STRINGTAB:
      {
        if (unlikely(stack.size() <= 0))
        {
          printf("stack underflow\n");
          ret = -EOVERFLOW;
          break;
        }
        val = get_i64(stack);
        if (unlikely(stack.size() >= stackbound))
        {
          printf("stack overflow\n");
          ret = -EOVERFLOW;
          break;
        }
        if (unlikely(val < 0 || (size_t)val >= st.blocks.size()))
        {
          printf("stringtab overflow\n");
          ret = -EOVERFLOW;
          break;
        }
        stack.push_back(st.blocks.at(val));
        break;
      }
      case STIRBCE_OPCODE_PUSH_NEW_DICT:
      {
        if (unlikely(stack.size() >= stackbound))
        {
          printf("stack overflow\n");
          ret = -EOVERFLOW;
          break;
        }
        stack.push_back(new std::map<std::string, memblock>());
        break;
      }
      case STIRBCE_OPCODE_PUSH_DBL:
      {
        if (unlikely(ip+8 >= microsz))
        {
          printf("microprogram overflow\n");
          ret = -EFAULT;
          break;
        }
        if (unlikely(stack.size() >= stackbound))
        {
          printf("stack overflow\n");
          ret = -EOVERFLOW;
          break;
        }
        uint64_t u64 = 
                      ((((uint64_t)microprogram[ip+0])<<56) |
                      (((uint64_t)microprogram[ip+1])<<48) |
                      (((uint64_t)microprogram[ip+2])<<40) |
                      (((uint64_t)microprogram[ip+3])<<32) |
                      (((uint64_t)microprogram[ip+4])<<24) |
                      (((uint64_t)microprogram[ip+5])<<16) |
                      (((uint64_t)microprogram[ip+6])<<8) |
                                  microprogram[ip+7]);
        double dbl;
        memcpy(&dbl, &u64, 8);
        stack.push_back(dbl);
        ip += 8;
        break;
      }
      case STIRBCE_OPCODE_FUN_HEADER:
      {
        printf("ran into function without being called\n");
        ret = -EFAULT;
        break;
        if (unlikely(ip+8 >= microsz))
        {
          printf("microprogram overflow\n");
          ret = -EFAULT;
          break;
        }
        if (unlikely(stack.size() >= stackbound))
        {
          printf("stack overflow\n");
          ret = -EOVERFLOW;
          break;
        }
        uint64_t u64 = 
                      ((((uint64_t)microprogram[ip+0])<<56) |
                      (((uint64_t)microprogram[ip+1])<<48) |
                      (((uint64_t)microprogram[ip+2])<<40) |
                      (((uint64_t)microprogram[ip+3])<<32) |
                      (((uint64_t)microprogram[ip+4])<<24) |
                      (((uint64_t)microprogram[ip+5])<<16) |
                      (((uint64_t)microprogram[ip+6])<<8) |
                                  microprogram[ip+7]);
        double dbl;
        memcpy(&dbl, &u64, 8);
        if ((double)(unsigned)dbl != dbl)
        {
          printf("function argument count not an integer\n");
          ret = -EINVAL;
          break;
        }
        ip += 8;
        break;
      }
      case STIRBCE_OPCODE_CALL_EQ:
        if (unlikely(stack.size() < 2))
        {
          printf("stack underflow\n");
          ret = -EOVERFLOW;
          break;
        }
        val = get_dbl(stack);
        val2 = get_dbl(stack);
        stack.push_back(!!(val == val2));
        break;
      case STIRBCE_OPCODE_CALL_LOGICAL_NOT:
        if (unlikely(stack.size() < 1))
        {
          printf("stack underflow\n");
          ret = -EOVERFLOW;
          break;
        }
        val = get_dbl(stack);
        stack.push_back(!val);
        break;
      case STIRBCE_OPCODE_CALL_BITWISE_NOT:
        if (unlikely(stack.size() < 1))
        {
          printf("stack underflow\n");
          ret = -EOVERFLOW;
          break;
        }
        val = get_i64(stack);
        stack.push_back(~val);
        break;
      case STIRBCE_OPCODE_CALL_UNARY_MINUS:
        if (unlikely(stack.size() < 1))
        {
          printf("stack underflow\n");
          ret = -EOVERFLOW;
          break;
        }
        val = get_dbl(stack);
        stack.push_back(-val);
        break;
      case STIRBCE_OPCODE_CALL_NE:
        if (unlikely(stack.size() < 2))
        {
          printf("stack underflow\n");
          ret = -EOVERFLOW;
          break;
        }
        val = get_dbl(stack);
        val2 = get_dbl(stack);
        stack.push_back(!!(val != val2));
        break;
      case STIRBCE_OPCODE_CALL_LT:
        if (unlikely(stack.size() < 2))
        {
          printf("stack underflow\n");
          ret = -EOVERFLOW;
          break;
        }
        val = get_dbl(stack);
        val2 = get_dbl(stack);
        stack.push_back(!!(val < val2));
        break;
      case STIRBCE_OPCODE_CALL_GT:
        if (unlikely(stack.size() < 2))
        {
          printf("stack underflow\n");
          ret = -EOVERFLOW;
          break;
        }
        val = get_dbl(stack);
        val2 = get_dbl(stack);
        stack.push_back(!!(val > val2));
        break;
      case STIRBCE_OPCODE_CALL_LE:
        if (unlikely(stack.size() < 2))
        {
          printf("stack underflow\n");
          ret = -EOVERFLOW;
          break;
        }
        val = get_dbl(stack);
        val2 = get_dbl(stack);
        stack.push_back(!!(val <= val2));
        break;
      case STIRBCE_OPCODE_CALL_GE:
        if (unlikely(stack.size() < 2))
        {
          printf("stack underflow\n");
          ret = -EOVERFLOW;
          break;
        }
        val = get_dbl(stack);
        val2 = get_dbl(stack);
        stack.push_back(!!(val >= val2));
        break;
      case STIRBCE_OPCODE_CALL_LOGICAL_AND:
        if (unlikely(stack.size() < 2))
        {
          printf("stack underflow\n");
          ret = -EOVERFLOW;
          break;
        }
        val = get_i64(stack);
        val2 = get_i64(stack);
        stack.push_back(!!(val && val2));
        break;
      case STIRBCE_OPCODE_CALL_LOGICAL_OR:
        if (unlikely(stack.size() < 2))
        {
          printf("stack underflow\n");
          ret = -EOVERFLOW;
          break;
        }
        val = get_i64(stack);
        val2 = get_i64(stack);
        stack.push_back(!!(val || val2));
        break;
      case STIRBCE_OPCODE_CALL_BITWISE_AND:
        if (unlikely(stack.size() < 2))
        {
          printf("stack underflow\n");
          ret = -EOVERFLOW;
          break;
        }
        val = get_i64(stack);
        val2 = get_i64(stack);
        stack.push_back((val & val2));
        break;
      case STIRBCE_OPCODE_CALL_BITWISE_OR:
        if (unlikely(stack.size() < 2))
        {
          printf("stack underflow\n");
          ret = -EOVERFLOW;
          break;
        }
        val = get_i64(stack);
        val2 = get_i64(stack);
        stack.push_back((val | val2));
        break;
      case STIRBCE_OPCODE_CALL_BITWISE_XOR:
        if (unlikely(stack.size() < 2))
        {
          printf("stack underflow\n");
          ret = -EOVERFLOW;
          break;
        }
        val = get_i64(stack);
        val2 = get_i64(stack);
        stack.push_back((val ^ val2));
        break;
      case STIRBCE_OPCODE_CALL_ADD:
        if (unlikely(stack.size() < 2))
        {
          printf("stack underflow\n");
          ret = -EOVERFLOW;
          break;
        }
        val = get_dbl(stack);
        val2 = get_dbl(stack);
        stack.push_back(val + val2);
        break;
      case STIRBCE_OPCODE_CALL_SUB:
        if (unlikely(stack.size() < 2))
        {
          printf("stack underflow\n");
          ret = -EOVERFLOW;
          break;
        }
        val = get_dbl(stack);
        val2 = get_dbl(stack);
        stack.push_back(val - val2);
        break;
      case STIRBCE_OPCODE_CALL_MUL:
        if (unlikely(stack.size() < 2))
        {
          printf("stack underflow\n");
          ret = -EOVERFLOW;
          break;
        }
        val = get_dbl(stack);
        val2 = get_dbl(stack);
        stack.push_back(val * val2);
        break;
      case STIRBCE_OPCODE_CALL_DIV:
        if (unlikely(stack.size() < 2))
        {
          printf("stack underflow\n");
          ret = -EOVERFLOW;
          break;
        }
        val = get_dbl(stack);
        val2 = get_dbl(stack);
        if (unlikely(val2 == 0))
        {
          printf("div by 0\n");
          ret = -EDOM;
          break;
        }
        stack.push_back(val / val2);
        break;
      case STIRBCE_OPCODE_CALL_MOD:
        if (unlikely(stack.size() < 2))
        {
          printf("stack underflow\n");
          ret = -EOVERFLOW;
          break;
        }
        val = get_i64(stack);
        val2 = get_i64(stack);
        if (unlikely(val2 == 0))
        {
          printf("div by 0\n");
          ret = -EDOM;
          break;
        }
        stack.push_back(val % val2);
        break;
      case STIRBCE_OPCODE_CALL_SHL:
        if (unlikely(stack.size() < 2))
        {
          printf("stack underflow\n");
          ret = -EOVERFLOW;
          break;
        }
        val = get_i64(stack);
        val2 = get_i64(stack);
        stack.push_back(val << val2);
        break;
      case STIRBCE_OPCODE_CALL_SHR:
        if (unlikely(stack.size() < 2))
        {
          printf("stack underflow\n");
          ret = -EOVERFLOW;
          break;
        }
        val = get_i64(stack);
        val2 = get_i64(stack);
        stack.push_back(val >> val2);
        break;
      case STIRBCE_OPCODE_LISTLEN:
      {
        if (unlikely(stack.size() < 1))
        {
          printf("stack underflow\n");
          ret = -EOVERFLOW;
          break;
        }
        memblock mbar = stack.back(); stack.pop_back();
        if (mbar.type != memblock::T_V)
        {
          printf("invalid type\n");
          ret = -EINVAL;
          break;
        }
        stack.push_back(mbar.u.v->size());
        break;
      }
      case STIRBCE_OPCODE_LISTSET:
      {
        if (unlikely(stack.size() < 2))
        {
          printf("stack underflow\n");
          ret = -EOVERFLOW;
          break;
        }
        memblock mbit = stack.back(); stack.pop_back();
        int64_t nr = get_i64(stack);
        memblock mbar = stack.back(); stack.pop_back();
        if (mbar.type != memblock::T_V)
        {
          printf("invalid type\n");
          ret = -EINVAL;
          break;
        }
        if (nr < 0 || (size_t)nr >= mbar.u.v->size())
        {
          printf("overflow\n");
          ret = -EOVERFLOW;
          break;
        }
        mbar.u.v->at(nr) = mbit;
        break;
      }
      case STIRBCE_OPCODE_LISTGET:
      {
        if (unlikely(stack.size() < 2))
        {
          printf("stack underflow\n");
          ret = -EOVERFLOW;
          break;
        }
        int64_t nr = get_i64(stack);
        memblock mbar = stack.back(); stack.pop_back();
        if (mbar.type != memblock::T_V)
        {
          printf("invalid type\n");
          ret = -EINVAL;
          break;
        }
        if (nr < 0 || (size_t)nr >= mbar.u.v->size())
        {
          printf("overflow\n");
          ret = -EOVERFLOW;
          break;
        }
        stack.push_back(mbar.u.v->at(nr));
        break;
      }
      case STIRBCE_OPCODE_APPEND:
      {
        if (unlikely(stack.size() < 2))
        {
          printf("stack underflow\n");
          ret = -EOVERFLOW;
          break;
        }
        memblock mbit = stack.back(); stack.pop_back();
        memblock mbar = stack.back(); stack.pop_back();
        if (mbar.type != memblock::T_V)
        {
          printf("invalid type\n");
          ret = -EINVAL;
          break;
        }
        mbar.u.v->push_back(mbit);
        break;
      }
      case STIRBCE_OPCODE_PUSH_NIL:
      {
        if (unlikely(stack.size() >= stackbound))
        {
          printf("stack overflow\n");
          ret = -EOVERFLOW;
          break;
        }
        stack.push_back(memblock());
        break;
      }
      case STIRBCE_OPCODE_PUSH_TRUE:
      {
        if (unlikely(stack.size() >= stackbound))
        {
          printf("stack overflow\n");
          ret = -EOVERFLOW;
          break;
        }
        stack.push_back(memblock(true));
        break;
      }
      case STIRBCE_OPCODE_PUSH_FALSE:
      {
        if (unlikely(stack.size() >= stackbound))
        {
          printf("stack overflow\n");
          ret = -EOVERFLOW;
          break;
        }
        stack.push_back(memblock(false));
        break;
      }
      case STIRBCE_OPCODE_BOOLEANIFY:
      {
        if (unlikely(stack.size() < 1))
        {
          printf("stack underflow\n");
          ret = -EOVERFLOW;
          break;
        }
        stack.push_back(memblock(get_dbl(stack) ? true : false));
        break;
      }
      case STIRBCE_OPCODE_FUN_JMP_ADDR:
      {
        if (unlikely(stack.size() < 1))
        {
          printf("stack underflow\n");
          ret = -EOVERFLOW;
          break;
        }
        memblock mbit = stack.back(); stack.pop_back();
        if (mbit.type != memblock::T_F)
        {
          printf("invalid type\n");
          ret = -EINVAL;
          break;
        }
        stack.push_back(memblock(0.0)); // FIXME implement
        std::terminate();
        break;
      }
      case STIRBCE_OPCODE_STRAPPEND:
      {
        if (unlikely(stack.size() < 2))
        {
          printf("stack underflow\n");
          ret = -EOVERFLOW;
          break;
        }
        memblock mbextend = stack.back(); stack.pop_back();
        memblock mbbase = stack.back(); stack.pop_back();
        if (mbbase.type != memblock::T_S || mbextend.type != memblock::T_S)
        {
          printf("invalid type\n");
          ret = -EINVAL;
          break;
        }
        stack.push_back(new std::string(*mbbase.u.s + *mbextend.u.s));
        break;
      }
      default:
        printf("invalid opcode\n");
        ret = -EILSEQ;
        break;
    }
  }
  return ret;
}

void store_d(std::vector<uint8_t> &v, double d)
{
  uint64_t val;
  memcpy(&val, &d, 8);
  v.push_back(val>>56);
  v.push_back(val>>48);
  v.push_back(val>>40);
  v.push_back(val>>32);
  v.push_back(val>>24);
  v.push_back(val>>16);
  v.push_back(val>>8);
  v.push_back(val);
}
