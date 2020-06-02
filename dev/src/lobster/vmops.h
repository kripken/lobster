#include "lobster/geom.h"
#include "lobster/vmdata.h"
#include "lobster/natreg.h"
#include "lobster/bytecode_generated.h"

namespace lobster {

#if RTT_ENABLED
    #define VMTYPEEQ(val, vt) VMASSERT((val).type == (vt))
#else
    #define VMTYPEEQ(val, vt) { (void)(val); (void)(vt); }
#endif

VM_INS_RET VM::U_PUSHINT(int x) { VM_PUSH(Value(x)); VM_RET; }
VM_INS_RET VM::U_PUSHFLT(int x) { int2float i2f; i2f.i = x; VM_PUSH(Value(i2f.f)); VM_RET; }
VM_INS_RET VM::U_PUSHNIL() { VM_PUSH(Value()); VM_RET; }

VM_INS_RET VM::U_PUSHINT64(int a, int b) {
    auto v = Int64FromInts(a, b);
    VM_PUSH(Value(v));
    VM_RET;
}

VM_INS_RET VM::U_PUSHFLT64(int a, int b) {
    int2float64 i2f;
    i2f.i = Int64FromInts(a, b);
    VM_PUSH(Value(i2f.f));
    VM_RET;
}

VM_INS_RET VM::U_PUSHFUN(int start VM_COMMA VM_OP_ARGS_CALL) {
    #ifdef VM_COMPILED_CODE_MODE
        (void)start;
    #else
        auto fcont = start;
    #endif
    VM_PUSH(Value(InsPtr(fcont)));
    VM_RET;
}

VM_INS_RET VM::U_PUSHSTR(int i) {
    // FIXME: have a way that constant strings can stay in the bytecode,
    // or at least preallocate them all
    auto &s = constant_strings[i];
    if (!s) {
        auto fb_s = bcf->stringtable()->Get(i);
        s = NewString(fb_s->string_view());
    }
    #if STRING_CONSTANTS_KEEP
        s->Inc();
    #endif
    VM_PUSH(Value(s));
    VM_RET;
}

VM_INS_RET VM::U_INCREF(int off) {
    VM_TOPM(off).LTINCRTNIL();
    VM_RET;
}

VM_INS_RET VM::U_KEEPREFLOOP(int off, int ki) {
    VM_TOPM(ki).LTDECRTNIL(*this);
    VM_TOPM(ki) = VM_TOPM(off);
    VM_RET;
}

VM_INS_RET VM::U_KEEPREF(int off, int ki) {
    VM_TOPM(ki) = VM_TOPM(off);
    VM_RET;
}

VM_INS_RET VM::U_CALL(int f VM_COMMA VM_OP_ARGS_CALL) {
    #ifdef VM_COMPILED_CODE_MODE
        (void)f;
        block_t fun = 0;  // Dynamic calls need this set, but for CALL it is ignored.
    #else
        auto fun = f;
        auto fcont = ip - codestart;
    #endif
    StartStackFrame(InsPtr(fcont));
    FunIntroPre(InsPtr(fun));
    VM_RET;
}

VM_INS_RET VM::U_CALLVCOND(VM_OP_ARGS_CALL) {
    // FIXME: don't need to check for function value again below if false
    if (!VM_TOP().True()) {
        VM_POP();
        #ifdef VM_COMPILED_CODE_MODE
            next_call_target = 0;
        #endif
    } else {
        U_CALLV(VM_FC_PASS_THRU);
    }
    VM_RET;
}

VM_INS_RET VM::U_CALLV(VM_OP_ARGS_CALL) {
    Value fun = VM_POP();
    VMTYPEEQ(fun, V_FUNCTION);
    #ifndef VM_COMPILED_CODE_MODE
        auto fcont = ip - codestart;
    #endif
    StartStackFrame(InsPtr(fcont));
    FunIntroPre(fun.ip());
    VM_RET;
}

VM_INS_RET VM::U_DDCALL(int vtable_idx, int stack_idx VM_COMMA VM_OP_ARGS_CALL) {
    auto self = VM_TOPM(stack_idx);
    VMTYPEEQ(self, V_CLASS);
    auto start = self.oval()->ti(*this).vtable_start;
    auto fun = vtables[start + vtable_idx];
    #ifdef VM_COMPILED_CODE_MODE
    #else
        auto fcont = ip - codestart;
        assert(fun.f >= 0);
    #endif
    StartStackFrame(InsPtr(fcont));
    FunIntroPre(fun);
    VM_RET;
}

VM_INS_RET VM::U_FUNSTART(VM_OP_ARGS) {
    #ifdef VM_COMPILED_CODE_MODE
        FunIntro(ip);
    #else
        VMASSERT(false);
    #endif
    VM_RET;
}

VM_INS_RET VM::U_RETURN(int df, int nrv) {
    FunOut(df, nrv);
    VM_RET;
}

VM_INS_RET VM::U_ENDSTATEMENT(int line, int fileidx) {
    #ifdef NDEBUG
        (void)line;
        (void)fileidx;
    #else
        if (trace != TraceMode::OFF) {
            auto &sd = TraceStream();
            append(sd, bcf->filenames()->Get(fileidx)->string_view(), "(", line, ")");
            if (trace == TraceMode::TAIL) sd += "\n"; else LOG_PROGRAM(sd);
        }
    #endif
    assert(sp == stackframes.back().spstart + stack);
    VM_RET;
}

VM_INS_RET VM::U_EXIT(int tidx) {
    if (tidx >= 0) EndEval(VM_POP(), GetTypeInfo((type_elem_t)tidx));
    else EndEval(Value(), GetTypeInfo(TYPE_ELEM_ANY));
    VM_TERMINATE;
}

VM_INS_RET VM::U_CONT1(int nfi) {
    auto nf = nfr.nfuns[nfi];
    nf->cont1(*this);
    VM_RET;
}

VM_JMP_RET VM::ForLoop(iint len) {
    #ifndef VM_COMPILED_CODE_MODE
        auto cont = *ip++;
    #endif
    auto &i = VM_TOPM(1);
    TYPE_ASSERT(i.type == V_INT);
    i.setival(i.ival() + 1);
    if (i.ival() < len) {
        #ifdef VM_COMPILED_CODE_MODE
            return true;
        #else
            ip = cont + codestart;
        #endif
    } else {
        (void)VM_POP(); /* iter */
        (void)VM_POP(); /* i */
        #ifdef VM_COMPILED_CODE_MODE
            return false;
        #endif
    }
}

#define FORELEM(L) \
    auto &iter = VM_TOP(); \
    auto i = VM_TOPM(1).ival(); \
    assert(i < L); \

VM_JMP_RET VM::U_IFOR() { return ForLoop(VM_TOP().ival()); VM_RET; }
VM_JMP_RET VM::U_VFOR() { return ForLoop(VM_TOP().vval()->len); VM_RET; }
VM_JMP_RET VM::U_SFOR() { return ForLoop(VM_TOP().sval()->len); VM_RET; }

VM_INS_RET VM::U_IFORELEM()    { FORELEM(iter.ival()); (void)iter; VM_PUSH(i); VM_RET; }
VM_INS_RET VM::U_VFORELEM()    { FORELEM(iter.vval()->len); iter.vval()->AtVW(*this, i); VM_RET; }
VM_INS_RET VM::U_VFORELEMREF() { FORELEM(iter.vval()->len); auto el = iter.vval()->At(i); el.LTINCRTNIL(); VM_PUSH(el); VM_RET; }
VM_INS_RET VM::U_SFORELEM()    { FORELEM(iter.sval()->len); VM_PUSH(Value(((uint8_t *)iter.sval()->data())[i])); VM_RET; }

VM_INS_RET VM::U_FORLOOPI() {
    auto &i = VM_TOPM(1);  // This relies on for being inlined, otherwise it would be 2.
    TYPE_ASSERT(i.type == V_INT);
    VM_PUSH(i);
    VM_RET;
}

VM_INS_RET VM::U_BCALLRETV(int nfi) {
    BCallProf();
    auto nf = nfr.nfuns[nfi];
    nf->fun.fV(*this);
    VM_RET;
}
VM_INS_RET VM::U_BCALLREFV(int nfi) {
    BCallProf();
    auto nf = nfr.nfuns[nfi];
    nf->fun.fV(*this);
    // This can only pop a single value, not called for structs.
    VM_POP().LTDECRTNIL(*this);
    VM_RET;
}
VM_INS_RET VM::U_BCALLUNBV(int nfi) {
    BCallProf();
    auto nf = nfr.nfuns[nfi];
    nf->fun.fV(*this);
    // This can only pop a single value, not called for structs.
    VM_POP();
    VM_RET;
}

#define BCALLOPH(PRE,N,DECLS,ARGS,RETOP) VM_INS_RET VM::U_BCALL##PRE##N(int nfi) { \
    BCallProf(); \
    auto nf = nfr.nfuns[nfi]; \
    DECLS; \
    Value v = nf->fun.f##N ARGS; \
    RETOP; \
    VM_RET; \
}

#define BCALLOP(N,DECLS,ARGS) \
    BCALLOPH(RET,N,DECLS,ARGS,VM_PUSH(v);BCallRetCheck(nf)) \
    BCALLOPH(REF,N,DECLS,ARGS,v.LTDECRTNIL(*this)) \
    BCALLOPH(UNB,N,DECLS,ARGS,(void)v)

BCALLOP(0, {}, (*this));
BCALLOP(1, auto a0 = VM_POP(), (*this, a0));
BCALLOP(2, auto a1 = VM_POP();auto a0 = VM_POP(), (*this, a0, a1));
BCALLOP(3, auto a2 = VM_POP();auto a1 = VM_POP();auto a0 = VM_POP(), (*this, a0, a1, a2));
BCALLOP(4, auto a3 = VM_POP();auto a2 = VM_POP();auto a1 = VM_POP();auto a0 = VM_POP(), (*this, a0, a1, a2, a3));
BCALLOP(5, auto a4 = VM_POP();auto a3 = VM_POP();auto a2 = VM_POP();auto a1 = VM_POP();auto a0 = VM_POP(), (*this, a0, a1, a2, a3, a4));
BCALLOP(6, auto a5 = VM_POP();auto a4 = VM_POP();auto a3 = VM_POP();auto a2 = VM_POP();auto a1 = VM_POP();auto a0 = VM_POP(), (*this, a0, a1, a2, a3, a4, a5));
BCALLOP(7, auto a6 = VM_POP();auto a5 = VM_POP();auto a4 = VM_POP();auto a3 = VM_POP();auto a2 = VM_POP();auto a1 = VM_POP();auto a0 = VM_POP(), (*this, a0, a1, a2, a3, a4, a5, a6));

VM_INS_RET VM::U_ASSERTR(int line, int fileidx, int stringidx) {
    (void)line;
    (void)fileidx;
    if (!VM_TOP().True()) {
        Error(cat(
            #ifdef VM_COMPILED_CODE_MODE
                bcf->filenames()->Get(fileidx)->string_view(), "(", line, "): ",
            #endif
            "assertion failed: ", bcf->stringtable()->Get(stringidx)->string_view()));
    }
    VM_RET;
}

VM_INS_RET VM::U_ASSERT(int line, int fileidx, int stringidx) {
    U_ASSERTR(line, fileidx, stringidx);
    VM_POP();
    VM_RET;
}

VM_INS_RET VM::U_NEWVEC(int ty, int len) {
    auto type = (type_elem_t)ty;
    auto vec = NewVec(len, len, type);
    if (len) vec->Init(*this, VM_TOPPTR() - len * vec->width, false);
    VM_POPN(len * (int)vec->width);
    VM_PUSH(Value(vec));
    VM_RET;
}

VM_INS_RET VM::U_NEWOBJECT(int ty) {
    auto type = (type_elem_t)ty;
    auto len = GetTypeInfo(type).len;
    auto vec = NewObject(len, type);
    if (len) vec->Init(*this, VM_TOPPTR() - len, len, false);
    VM_POPN(len);
    VM_PUSH(Value(vec));
    VM_RET;
}

VM_INS_RET VM::U_POP()     { VM_POP(); VM_RET; }
VM_INS_RET VM::U_POPREF()  { auto x = VM_POP(); x.LTDECRTNIL(*this); VM_RET; }

VM_INS_RET VM::U_POPV(int len)    { VM_POPN(len); VM_RET; }
VM_INS_RET VM::U_POPVREF(int len) { while (len--) VM_POP().LTDECRTNIL(*this); VM_RET; }

VM_INS_RET VM::U_DUP()    { auto x = VM_TOP(); VM_PUSH(x); VM_RET; }


#define GETARGS() Value b = VM_POP(); Value a = VM_POP()
#define TYPEOP(op, extras, av, bv, res) \
    if constexpr ((extras & 1) != 0) if (bv == 0) Div0(); \
    if constexpr ((extras & 2) != 0) res = fmod((double)av, (double)bv); else res = av op bv;

#define _IOP(op, extras) \
    Value res; \
    TYPE_ASSERT(a.type == V_INT && b.type == V_INT); \
    TYPEOP(op, extras, a.ival(), b.ival(), res)
#define _FOP(op, extras) \
    Value res; \
    TYPE_ASSERT(a.type == V_FLOAT && b.type == V_FLOAT); \
    TYPEOP(op, extras, a.fval(), b.fval(), res)

#define _VOP(op, extras, V_T, field, withscalar, geta) { \
    if (withscalar) { \
        auto b = VM_POP(); \
        VMTYPEEQ(b, V_T) \
        auto veca = geta; \
        for (int j = 0; j < len; j++) { \
            auto &a = veca[j]; \
            VMTYPEEQ(a, V_T) \
            auto bv = b.field(); \
            TYPEOP(op, extras, a.field(), bv, a) \
        } \
    } else { \
        VM_POPN(len); \
        auto vecb = VM_TOPPTR(); \
        auto veca = geta; \
        for (int j = 0; j < len; j++) { \
            auto b = vecb[j]; \
            VMTYPEEQ(b, V_T) \
            auto &a = veca[j]; \
            VMTYPEEQ(a, V_T) \
            auto bv = b.field(); \
            TYPEOP(op, extras, a.field(), bv, a) \
        } \
    } \
}
#define STCOMPEN(op, init, andor) { \
    VM_POPN(len); \
    auto vecb = VM_TOPPTR(); \
    VM_POPN(len); \
    auto veca = VM_TOPPTR(); \
    auto all = init; \
    for (int j = 0; j < len; j++) { \
        all = all andor veca[j].any() op vecb[j].any(); \
    } \
    VM_PUSH(all); \
    VM_RET; \
}

#define _IVOP(op, extras, withscalar, geta) _VOP(op, extras, V_INT, ival, withscalar, geta)
#define _FVOP(op, extras, withscalar, geta) _VOP(op, extras, V_FLOAT, fval, withscalar, geta)

#define _SCAT() Value res = NewString(a.sval()->strv(), b.sval()->strv())

#define ACOMPEN(op)        { GETARGS(); Value res = a.any() op b.any();  VM_PUSH(res); VM_RET; }
#define IOP(op, extras)    { GETARGS(); _IOP(op, extras);                VM_PUSH(res); VM_RET; }
#define FOP(op, extras)    { GETARGS(); _FOP(op, extras);                VM_PUSH(res); VM_RET; }

#define LOP(op)            { GETARGS(); auto res = a.ip() op b.ip();     VM_PUSH(res); VM_RET; }

#define IVVOP(op, extras)  { _IVOP(op, extras, false, VM_TOPPTR() - len); VM_RET; }
#define FVVOP(op, extras)  { _FVOP(op, extras, false, VM_TOPPTR() - len); VM_RET; }
#define IVSOP(op, extras)  { _IVOP(op, extras, true, VM_TOPPTR() - len);  VM_RET; }
#define FVSOP(op, extras)  { _FVOP(op, extras, true, VM_TOPPTR() - len);  VM_RET; }

#define SOP(op)            { GETARGS(); Value res = *a.sval() op *b.sval(); VM_PUSH(res); VM_RET; }
#define SCAT()             { GETARGS(); _SCAT();                            VM_PUSH(res); VM_RET; }

// +  += I F Vif S
// -  -= I F Vif
// *  *= I F Vif
// /  /= I F Vif
// %  %= I F Vif

// <     I F Vif S
// >     I F Vif S
// <=    I F Vif S
// >=    I F Vif S
// ==    I F V   S   // FIXME differentiate struct / value / vector
// !=    I F V   S

// U-    I F Vif
// U!    A

VM_INS_RET VM::U_IVVADD(int len) { IVVOP(+,  0);  }
VM_INS_RET VM::U_IVVSUB(int len) { IVVOP(-,  0);  }
VM_INS_RET VM::U_IVVMUL(int len) { IVVOP(*,  0);  }
VM_INS_RET VM::U_IVVDIV(int len) { IVVOP(/,  1);  }
VM_INS_RET VM::U_IVVMOD(int len) { IVVOP(% , 1); }
VM_INS_RET VM::U_IVVLT(int len)  { IVVOP(<,  0);  }
VM_INS_RET VM::U_IVVGT(int len)  { IVVOP(>,  0);  }
VM_INS_RET VM::U_IVVLE(int len)  { IVVOP(<=, 0);  }
VM_INS_RET VM::U_IVVGE(int len)  { IVVOP(>=, 0);  }
VM_INS_RET VM::U_FVVADD(int len) { FVVOP(+,  0);  }
VM_INS_RET VM::U_FVVSUB(int len) { FVVOP(-,  0);  }
VM_INS_RET VM::U_FVVMUL(int len) { FVVOP(*,  0);  }
VM_INS_RET VM::U_FVVDIV(int len) { FVVOP(/,  1);  }
VM_INS_RET VM::U_FVVMOD(int len) { FVVOP(/ , 3); }
VM_INS_RET VM::U_FVVLT(int len)  { FVVOP(<,  0); }
VM_INS_RET VM::U_FVVGT(int len)  { FVVOP(>,  0); }
VM_INS_RET VM::U_FVVLE(int len)  { FVVOP(<=, 0); }
VM_INS_RET VM::U_FVVGE(int len)  { FVVOP(>=, 0); }

VM_INS_RET VM::U_IVSADD(int len) { IVSOP(+,  0);  }
VM_INS_RET VM::U_IVSSUB(int len) { IVSOP(-,  0);  }
VM_INS_RET VM::U_IVSMUL(int len) { IVSOP(*,  0);  }
VM_INS_RET VM::U_IVSDIV(int len) { IVSOP(/,  1);  }
VM_INS_RET VM::U_IVSMOD(int len) { IVSOP(% , 1); }
VM_INS_RET VM::U_IVSLT(int len)  { IVSOP(<,  0);  }
VM_INS_RET VM::U_IVSGT(int len)  { IVSOP(>,  0);  }
VM_INS_RET VM::U_IVSLE(int len)  { IVSOP(<=, 0);  }
VM_INS_RET VM::U_IVSGE(int len)  { IVSOP(>=, 0);  }
VM_INS_RET VM::U_FVSADD(int len) { FVSOP(+,  0);  }
VM_INS_RET VM::U_FVSSUB(int len) { FVSOP(-,  0);  }
VM_INS_RET VM::U_FVSMUL(int len) { FVSOP(*,  0);  }
VM_INS_RET VM::U_FVSDIV(int len) { FVSOP(/,  1);  }
VM_INS_RET VM::U_FVSMOD(int len) { FVSOP(/ , 3); }
VM_INS_RET VM::U_FVSLT(int len)  { FVSOP(<,  0); }
VM_INS_RET VM::U_FVSGT(int len)  { FVSOP(>,  0); }
VM_INS_RET VM::U_FVSLE(int len)  { FVSOP(<=, 0); }
VM_INS_RET VM::U_FVSGE(int len)  { FVSOP(>=, 0); }

VM_INS_RET VM::U_AEQ()  { ACOMPEN(==); }
VM_INS_RET VM::U_ANE()  { ACOMPEN(!=); }
VM_INS_RET VM::U_STEQ(int len) { STCOMPEN(==, true, &&); }
VM_INS_RET VM::U_STNE(int len) { STCOMPEN(!=, false, ||); }
VM_INS_RET VM::U_LEQ() { LOP(==); }
VM_INS_RET VM::U_LNE() { LOP(!=); }

VM_INS_RET VM::U_IADD() { IOP(+,  0); }
VM_INS_RET VM::U_ISUB() { IOP(-,  0); }
VM_INS_RET VM::U_IMUL() { IOP(*,  0); }
VM_INS_RET VM::U_IDIV() { IOP(/ , 1); }
VM_INS_RET VM::U_IMOD() { IOP(%,  1); }
VM_INS_RET VM::U_ILT()  { IOP(<,  0); }
VM_INS_RET VM::U_IGT()  { IOP(>,  0); }
VM_INS_RET VM::U_ILE()  { IOP(<=, 0); }
VM_INS_RET VM::U_IGE()  { IOP(>=, 0); }
VM_INS_RET VM::U_IEQ()  { IOP(==, 0); }
VM_INS_RET VM::U_INE()  { IOP(!=, 0); }

VM_INS_RET VM::U_FADD() { FOP(+,  0); }
VM_INS_RET VM::U_FSUB() { FOP(-,  0); }
VM_INS_RET VM::U_FMUL() { FOP(*,  0); }
VM_INS_RET VM::U_FDIV() { FOP(/,  1); }
VM_INS_RET VM::U_FMOD() { FOP(/,  3); }
VM_INS_RET VM::U_FLT()  { FOP(<,  0); }
VM_INS_RET VM::U_FGT()  { FOP(>,  0); }
VM_INS_RET VM::U_FLE()  { FOP(<=, 0); }
VM_INS_RET VM::U_FGE()  { FOP(>=, 0); }
VM_INS_RET VM::U_FEQ()  { FOP(==, 0); }
VM_INS_RET VM::U_FNE()  { FOP(!=, 0); }

VM_INS_RET VM::U_SADD() { SCAT();  }
VM_INS_RET VM::U_SSUB() { VMASSERT(0); VM_RET; }
VM_INS_RET VM::U_SMUL() { VMASSERT(0); VM_RET; }
VM_INS_RET VM::U_SDIV() { VMASSERT(0); VM_RET; }
VM_INS_RET VM::U_SMOD() { VMASSERT(0); VM_RET; }
VM_INS_RET VM::U_SLT()  { SOP(<);  }
VM_INS_RET VM::U_SGT()  { SOP(>);  }
VM_INS_RET VM::U_SLE()  { SOP(<=); }
VM_INS_RET VM::U_SGE()  { SOP(>=); }
VM_INS_RET VM::U_SEQ()  { SOP(==); }
VM_INS_RET VM::U_SNE()  { SOP(!=); }

VM_INS_RET VM::U_IUMINUS() { Value a = VM_POP(); VM_PUSH(Value(-a.ival())); VM_RET; }
VM_INS_RET VM::U_FUMINUS() { Value a = VM_POP(); VM_PUSH(Value(-a.fval())); VM_RET; }

VM_INS_RET VM::U_IVUMINUS(int len) {
    auto vec = VM_TOPPTR() - len;
    for (int i = 0; i < len; i++) {
        auto &a = vec[i];
        VMTYPEEQ(a, V_INT);
        a = -a.ival();
    }
    VM_RET;
}
VM_INS_RET VM::U_FVUMINUS(int len) {
    auto vec = VM_TOPPTR() - len;
    for (int i = 0; i < len; i++) {
        auto &a = vec[i];
        VMTYPEEQ(a, V_FLOAT);
        a = -a.fval();
    }
    VM_RET;
}

VM_INS_RET VM::U_LOGNOT() {
    Value a = VM_POP();
    VM_PUSH(!a.True());
    VM_RET;
}
VM_INS_RET VM::U_LOGNOTREF() {
    Value a = VM_POP();
    bool b = a.True();
    VM_PUSH(!b);
    VM_RET;
}

#define BITOP(op) { GETARGS(); VM_PUSH(a.ival() op b.ival()); VM_RET; }
VM_INS_RET VM::U_BINAND() { BITOP(&);  }
VM_INS_RET VM::U_BINOR()  { BITOP(|);  }
VM_INS_RET VM::U_XOR()    { BITOP(^);  }
VM_INS_RET VM::U_ASL()    { BITOP(<<); }
VM_INS_RET VM::U_ASR()    { BITOP(>>); }
VM_INS_RET VM::U_NEG()    { auto a = VM_POP(); VM_PUSH(~a.ival()); VM_RET; }

VM_INS_RET VM::U_I2F() {
    Value a = VM_POP();
    VMTYPEEQ(a, V_INT);
    VM_PUSH((float)a.ival());
    VM_RET;
}

VM_INS_RET VM::U_A2S(int ty) {
    Value a = VM_POP();
    VM_PUSH(ToString(a, GetTypeInfo((type_elem_t)ty)));
    VM_RET;
}

VM_INS_RET VM::U_ST2S(int ty) {
    auto &ti = GetTypeInfo((type_elem_t)ty);
    VM_POPN(ti.len);
    auto top = VM_TOPPTR();
    VM_PUSH(StructToString(top, ti));
    VM_RET;
}

VM_INS_RET VM::U_E2B() {
    Value a = VM_POP();
    VM_PUSH(a.True());
    VM_RET;
}

VM_INS_RET VM::U_E2BREF() {
    Value a = VM_POP();
    VM_PUSH(a.True());
    VM_RET;
}

VM_INS_RET VM::U_PUSHVAR(int vidx) {
    VM_PUSH(vars[vidx]);
    VM_RET;
}

VM_INS_RET VM::U_PUSHVARV(int vidx, int l) {
    tsnz_memcpy(VM_TOPPTR(), &vars[vidx], l);
    VM_PUSHN(l);
    VM_RET;
}

VM_INS_RET VM::U_PUSHFLD(int i) {
    Value r = VM_POP();
    VMASSERT(r.ref());
    assert(i < r.oval()->Len(*this));
    VM_PUSH(r.oval()->AtS(i));
    VM_RET;
}
VM_INS_RET VM::U_PUSHFLDMREF(int i) {
    Value r = VM_POP();
    if (!r.ref()) {
        VM_PUSH(r);
    } else {
        assert(i < r.oval()->Len(*this));
        VM_PUSH(r.oval()->AtS(i));
    }
    VM_RET;
}
VM_INS_RET VM::U_PUSHFLD2V(int i, int l) {
    Value r = VM_POP();
    VMASSERT(r.ref());
    assert(i + l <= r.oval()->Len(*this));
    tsnz_memcpy(VM_TOPPTR(), &r.oval()->AtS(i), l);
    VM_PUSHN(l);
    VM_RET;
}
VM_INS_RET VM::U_PUSHFLDV(int i, int l) {
    VM_POPN(l);
    auto val = *(VM_TOPPTR() + i);
    VM_PUSH(val);
    VM_RET;
}
VM_INS_RET VM::U_PUSHFLDV2V(int i, int rl, int l) {
    VM_POPN(l);
    t_memmove(VM_TOPPTR(), VM_TOPPTR() + i, rl);
    VM_PUSHN(rl);
    VM_RET;
}

VM_INS_RET VM::U_VPUSHIDXI()  { PushDerefIdxVector(VM_POP().ival()); VM_RET; }
VM_INS_RET VM::U_VPUSHIDXV(int l)  { PushDerefIdxVector(GrabIndex(l)); VM_RET; }
VM_INS_RET VM::U_VPUSHIDXIS(int w, int o) { PushDerefIdxVectorSub(VM_POP().ival(), w, o); VM_RET; }
VM_INS_RET VM::U_VPUSHIDXVS(int l, int w, int o) { PushDerefIdxVectorSub(GrabIndex(l), w, o); VM_RET; }
VM_INS_RET VM::U_NPUSHIDXI(int l)  { PushDerefIdxStruct(VM_POP().ival(), l); VM_RET; }
VM_INS_RET VM::U_SPUSHIDXI()  { PushDerefIdxString(VM_POP().ival()); VM_RET; }

VM_INS_RET VM::U_PUSHLOC(int i) {
    auto coro = VM_POP().cval();
    VM_PUSH(coro->GetVar(*this, i));
    VM_RET;
}

VM_INS_RET VM::U_PUSHLOCV(int i, int l) {
    auto coro = VM_POP().cval();
    tsnz_memcpy(VM_TOPPTR(), &coro->GetVar(*this, i), l);
    VM_PUSHN(l);
    VM_RET;
}

#ifdef VM_COMPILED_CODE_MODE
    #define GJUMP(N, V, C, P) VM_JMP_RET VM::U_##N() \
        { V; if (C) { P; return true; } else { return false; } }
#else
    #define GJUMP(N, V, C, P) VM_JMP_RET VM::U_##N() \
        { V; auto nip = *ip++; if (C) { ip = codestart + nip; P; } VM_RET; }
#endif

GJUMP(JUMP       ,                  , true     ,                 )
GJUMP(JUMPFAIL   , auto x = VM_POP(), !x.True(),                 )
GJUMP(JUMPFAILR  , auto x = VM_POP(), !x.True(), VM_PUSH(x)      )
GJUMP(JUMPNOFAIL , auto x = VM_POP(),  x.True(),                 )
GJUMP(JUMPNOFAILR, auto x = VM_POP(),  x.True(), VM_PUSH(x)      )

VM_INS_RET VM::U_JUMP_TABLE(int mini, int maxi, int table_start) {
    auto val = VM_POP().ival();
    if (val < mini || val > maxi) val = maxi + 1;
    auto target = vtables[(ssize_t)(table_start + val - mini)];
    JumpTo(target);
    VM_RET;
}

VM_INS_RET VM::U_ISTYPE(int ty) {
    auto to = (type_elem_t)ty;
    auto v = VM_POP();
    // Optimizer guarantees we don't have to deal with scalars.
    if (v.refnil()) VM_PUSH(v.ref()->tti == to);
    else VM_PUSH(GetTypeInfo(to).t == V_NIL);  // FIXME: can replace by fixed type_elem_t ?
    VM_RET;
}

VM_INS_RET VM::U_YIELD(VM_OP_ARGS_CALL) { CoYield(VM_FC_PASS_THRU); VM_RET; }

// This value never gets used anywhere, just a placeholder.
VM_INS_RET VM::U_COCL() { VM_PUSH(Value(0, V_YIELD)); VM_RET; }

VM_INS_RET VM::U_CORO(VM_OP_ARGS VM_COMMA VM_OP_ARGS_CALL) { CoNew(VM_IP_PASS_THRU VM_COMMA VM_FC_PASS_THRU); VM_RET; }

VM_INS_RET VM::U_COEND() { CoClean(); VM_RET; }

VM_INS_RET VM::U_LOGREAD(int vidx) {
    auto val = VM_POP();
    VM_PUSH(vml.LogGet(val, vidx));
    VM_RET;
}

VM_INS_RET VM::U_LOGWRITE(int vidx, int lidx) {
    vml.LogWrite(vars[vidx], lidx);
    VM_RET;
}

VM_INS_RET VM::U_ABORT() {
    SeriousError("VM internal error: abort");
    VM_RET;
}

void VM::PushDerefIdxVector(iint i) {
    Value r = VM_POP();
    VMASSERT(r.ref());
    auto v = r.vval();
    RANGECHECK(i, v->len, v);
    v->AtVW(*this, i);
}

void VM::PushDerefIdxVectorSub(iint i, int width, int offset) {
    Value r = VM_POP();
    VMASSERT(r.ref());
    auto v = r.vval();
    RANGECHECK(i, v->len, v);
    v->AtVWSub(*this, i, width, offset);
}

void VM::PushDerefIdxStruct(iint i, int l) {
    VM_POPN(l);
    auto val = *(VM_TOPPTR() + i);
    VM_PUSH(val);
}

void VM::PushDerefIdxString(iint i) {
    Value r = VM_POP();
    VMASSERT(r.ref());
    // Allow access of the terminating 0-byte.
    RANGECHECK(i, r.sval()->len + 1, r.sval());
    VM_PUSH(Value(((uint8_t *)r.sval()->data())[i]));
}

Value &VM::GetFieldLVal(iint i) {
    Value vec = VM_POP();
    #ifndef NDEBUG
        RANGECHECK(i, vec.oval()->Len(*this), vec.oval());
    #endif
    return vec.oval()->AtS(i);
}

Value &VM::GetFieldILVal(iint i) {
    Value vec = VM_POP();
    RANGECHECK(i, vec.oval()->Len(*this), vec.oval());
    return vec.oval()->AtS(i);
}

Value &VM::GetVecLVal(iint i) {
    Value vec = VM_POP();
    auto v = vec.vval();
    RANGECHECK(i, v->len, v);
    return *v->AtSt(i);
}

Value &VM::GetLocLVal(int i) {
    Value coro = VM_POP();
    VMTYPEEQ(coro, V_COROUTINE);
    return coro.cval()->GetVar(*this, i);
}

#pragma push_macro("LVAL")
#undef LVAL

#define LVAL(N, V) VM_INS_RET VM::U_VAR_##N(int vidx VM_COMMA_IF(V) VM_OP_ARGSN(V)) \
    { LV_##N(vars[vidx] VM_COMMA_IF(V) VM_OP_PASSN(V)); VM_RET; }
    LVALOPNAMES
#undef LVAL

#define LVAL(N, V) VM_INS_RET VM::U_FLD_##N(int i VM_COMMA_IF(V) VM_OP_ARGSN(V)) \
    { LV_##N(GetFieldLVal(i) VM_COMMA_IF(V) VM_OP_PASSN(V)); VM_RET; }
    LVALOPNAMES
#undef LVAL

#define LVAL(N, V) VM_INS_RET VM::U_LOC_##N(int i VM_COMMA_IF(V) VM_OP_ARGSN(V)) \
    { LV_##N(GetLocLVal(i) VM_COMMA_IF(V) VM_OP_PASSN(V)); VM_RET; }
    LVALOPNAMES
#undef LVAL

#define LVAL(N, V) VM_INS_RET VM::U_IDXVI_##N(VM_OP_ARGSN(V)) \
    { LV_##N(GetVecLVal(VM_POP().ival()) VM_COMMA_IF(V) VM_OP_PASSN(V)); VM_RET; }
    LVALOPNAMES
#undef LVAL

#define LVAL(N, V) VM_INS_RET VM::U_IDXVV_##N(int l VM_COMMA_IF(V) VM_OP_ARGSN(V)) \
    { LV_##N(GetVecLVal(GrabIndex(l)) VM_COMMA_IF(V) VM_OP_PASSN(V)); VM_RET; }
    LVALOPNAMES
#undef LVAL

#define LVAL(N, V) VM_INS_RET VM::U_IDXNI_##N(VM_OP_ARGSN(V)) \
    { LV_##N(GetFieldILVal(VM_POP().ival()) VM_COMMA_IF(V) VM_OP_PASSN(V)); VM_RET; }
    LVALOPNAMES
#undef LVAL

#pragma pop_macro("LVAL")

#define LVALCASES(N, B) void VM::LV_##N(Value &a) { Value b = VM_POP(); B; }
#define LVALCASER(N, B) void VM::LV_##N(Value &fa, int len) { B; }
#define LVALCASESTR(N, B, B2) void VM::LV_##N(Value &a) { Value b = VM_POP(); B; a.LTDECRTNIL(*this); B2; }

LVALCASER(IVVADD , _IVOP(+, 0, false, &fa))
LVALCASER(IVVADDR, _IVOP(+, 0, false, &fa))
LVALCASER(IVVSUB , _IVOP(-, 0, false, &fa))
LVALCASER(IVVSUBR, _IVOP(-, 0, false, &fa))
LVALCASER(IVVMUL , _IVOP(*, 0, false, &fa))
LVALCASER(IVVMULR, _IVOP(*, 0, false, &fa))
LVALCASER(IVVDIV , _IVOP(/, 1, false, &fa))
LVALCASER(IVVDIVR, _IVOP(/, 1, false, &fa))
LVALCASER(IVVMOD , VMASSERT(0); (void)fa; (void)len)
LVALCASER(IVVMODR, VMASSERT(0); (void)fa; (void)len)

LVALCASER(FVVADD , _FVOP(+, 0, false, &fa))
LVALCASER(FVVADDR, _FVOP(+, 0, false, &fa))
LVALCASER(FVVSUB , _FVOP(-, 0, false, &fa))
LVALCASER(FVVSUBR, _FVOP(-, 0, false, &fa))
LVALCASER(FVVMUL , _FVOP(*, 0, false, &fa))
LVALCASER(FVVMULR, _FVOP(*, 0, false, &fa))
LVALCASER(FVVDIV , _FVOP(/, 1, false, &fa))
LVALCASER(FVVDIVR, _FVOP(/, 1, false, &fa))

LVALCASER(IVSADD , _IVOP(+, 0, true,  &fa))
LVALCASER(IVSADDR, _IVOP(+, 0, true,  &fa))
LVALCASER(IVSSUB , _IVOP(-, 0, true,  &fa))
LVALCASER(IVSSUBR, _IVOP(-, 0, true,  &fa))
LVALCASER(IVSMUL , _IVOP(*, 0, true,  &fa))
LVALCASER(IVSMULR, _IVOP(*, 0, true,  &fa))
LVALCASER(IVSDIV , _IVOP(/, 1, true,  &fa))
LVALCASER(IVSDIVR, _IVOP(/, 1, true,  &fa))
LVALCASER(IVSMOD , VMASSERT(0); (void)fa; (void)len)
LVALCASER(IVSMODR, VMASSERT(0); (void)fa; (void)len)

LVALCASER(FVSADD , _FVOP(+, 0, true,  &fa))
LVALCASER(FVSADDR, _FVOP(+, 0, true,  &fa))
LVALCASER(FVSSUB , _FVOP(-, 0, true,  &fa))
LVALCASER(FVSSUBR, _FVOP(-, 0, true,  &fa))
LVALCASER(FVSMUL , _FVOP(*, 0, true,  &fa))
LVALCASER(FVSMULR, _FVOP(*, 0, true,  &fa))
LVALCASER(FVSDIV , _FVOP(/, 1, true,  &fa))
LVALCASER(FVSDIVR, _FVOP(/, 1, true,  &fa))

LVALCASES(IADD   , _IOP(+, 0); a = res;          )
LVALCASES(IADDR  , _IOP(+, 0); a = res; VM_PUSH(res))
LVALCASES(ISUB   , _IOP(-, 0); a = res;          )
LVALCASES(ISUBR  , _IOP(-, 0); a = res; VM_PUSH(res))
LVALCASES(IMUL   , _IOP(*, 0); a = res;          )
LVALCASES(IMULR  , _IOP(*, 0); a = res; VM_PUSH(res))
LVALCASES(IDIV   , _IOP(/, 1); a = res;          )
LVALCASES(IDIVR  , _IOP(/, 1); a = res; VM_PUSH(res))
LVALCASES(IMOD   , _IOP(%, 1); a = res;          )
LVALCASES(IMODR  , _IOP(%, 1); a = res; VM_PUSH(res))

LVALCASES(BINAND , _IOP(&,  0); a = res;          )
LVALCASES(BINANDR, _IOP(&,  0); a = res; VM_PUSH(res))
LVALCASES(BINOR  , _IOP(|,  0); a = res;          )
LVALCASES(BINORR , _IOP(|,  0); a = res; VM_PUSH(res))
LVALCASES(XOR    , _IOP(^,  0); a = res;          )
LVALCASES(XORR   , _IOP(^,  0); a = res; VM_PUSH(res))
LVALCASES(ASL    , _IOP(<<, 0); a = res;          )
LVALCASES(ASLR   , _IOP(<<, 0); a = res; VM_PUSH(res))
LVALCASES(ASR    , _IOP(>>, 0); a = res;          )
LVALCASES(ASRR   , _IOP(>>, 0); a = res; VM_PUSH(res))

LVALCASES(FADD   , _FOP(+, 0); a = res;          )
LVALCASES(FADDR  , _FOP(+, 0); a = res; VM_PUSH(res))
LVALCASES(FSUB   , _FOP(-, 0); a = res;          )
LVALCASES(FSUBR  , _FOP(-, 0); a = res; VM_PUSH(res))
LVALCASES(FMUL   , _FOP(*, 0); a = res;          )
LVALCASES(FMULR  , _FOP(*, 0); a = res; VM_PUSH(res))
LVALCASES(FDIV   , _FOP(/, 1); a = res;          )
LVALCASES(FDIVR  , _FOP(/, 1); a = res; VM_PUSH(res))

LVALCASESTR(SADD , _SCAT(),    a = res;          )
LVALCASESTR(SADDR, _SCAT(),    a = res; VM_PUSH(res))

#define OVERWRITE_VAR(a, b) { TYPE_ASSERT(a.type == b.type || a.type == V_NIL || b.type == V_NIL); a = b; }

void VM::LV_WRITE    (Value &a) { auto  b = VM_POP();                      OVERWRITE_VAR(a, b); }
void VM::LV_WRITER   (Value &a) { auto &b = VM_TOP();                      OVERWRITE_VAR(a, b); }
void VM::LV_WRITEREF (Value &a) { auto  b = VM_POP(); a.LTDECRTNIL(*this); OVERWRITE_VAR(a, b); }
void VM::LV_WRITERREF(Value &a) { auto &b = VM_TOP(); a.LTDECRTNIL(*this); OVERWRITE_VAR(a, b); }

#define WRITESTRUCT(DECS) \
    auto b = VM_TOPPTR() - l; \
    if (DECS) for (int i = 0; i < l; i++) (&a)[i].LTDECRTNIL(*this); \
    tsnz_memcpy(&a, b, l);

void VM::LV_WRITEV    (Value &a, int l) { WRITESTRUCT(false); VM_POPN(l); }
void VM::LV_WRITERV   (Value &a, int l) { WRITESTRUCT(false); }
void VM::LV_WRITEREFV (Value &a, int l) { WRITESTRUCT(true); VM_POPN(l); }
void VM::LV_WRITERREFV(Value &a, int l) { WRITESTRUCT(true); }

#define PPOP(name, ret, op, pre, accessor) void VM::LV_##name(Value &a) { \
    if (ret && !pre) VM_PUSH(a); \
    a.set##accessor(a.accessor() op 1); \
    if (ret && pre) VM_PUSH(a); \
}

PPOP(IPP  , false, +, true , ival)
PPOP(IPPR , true , +, true , ival)
PPOP(IMM  , false, -, true , ival)
PPOP(IMMR , true , -, true , ival)
PPOP(IPPP , false, +, false, ival)
PPOP(IPPPR, true , +, false, ival)
PPOP(IMMP , false, -, false, ival)
PPOP(IMMPR, true , -, false, ival)
PPOP(FPP  , false, +, true , fval)
PPOP(FPPR , true , +, true , fval)
PPOP(FMM  , false, -, true , fval)
PPOP(FMMR , true , -, true , fval)
PPOP(FPPP , false, +, false, fval)
PPOP(FPPPR, true , +, false, fval)
PPOP(FMMP , false, -, false, fval)
PPOP(FMMPR, true , -, false, fval)




}  // namespace lobster
