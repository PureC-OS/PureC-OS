//! Tree-walk интерпретатор PureL (фиксированные таблицы, без alloc).

use crate::ast::{Arena, BinOp, NIL, Node, NodeId, StringPool, UnOp};

const MAX_VARS: usize = 64;
const MAX_FUNCS: usize = 16;
const MAX_CALL_DEPTH: usize = 32;
const STR_CAP: usize = 48;

#[derive(Clone, Copy)]
pub enum Value {
    Nil,
    Bool(bool),
    Int(i64),
    Str(u16), // index in shared StringPool
}

impl Value {
    pub fn is_truthy(self) -> bool {
        match self {
            Value::Nil => false,
            Value::Bool(b) => b,
            Value::Int(0) => false,
            Value::Int(_) => true,
            Value::Str(_) => true,
        }
    }

    pub fn as_int(self) -> Option<i64> {
        match self {
            Value::Int(n) => Some(n),
            Value::Bool(true) => Some(1),
            Value::Bool(false) | Value::Nil => Some(0),
            Value::Str(_) => None,
        }
    }
}

struct VarSlot {
    name: u16,
    value: Value,
    used: bool,
}

struct FuncSlot {
    name: u16,
    params: NodeId,
    body: NodeId,
    used: bool,
}

struct Frame {
    /// base index in vars for this frame's locals — we use a simple flat env with shadowing
    /// by searching from the end. On call we push a marker.
    marker: usize,
}

pub struct Vm<'a> {
    arena: &'a Arena,
    strings: &'a mut StringPool,
    vars: [VarSlot; MAX_VARS],
    var_len: usize,
    funcs: [FuncSlot; MAX_FUNCS],
    frames: [Frame; MAX_CALL_DEPTH],
    frame_len: usize,
    pub error: Option<&'static str>,
    returning: Option<Value>,
}

impl<'a> Vm<'a> {
    pub fn new(arena: &'a Arena, strings: &'a mut StringPool) -> Self {
        Self {
            arena,
            strings,
            vars: [VarSlot {
                name: 0,
                value: Value::Nil,
                used: false,
            }; MAX_VARS],
            var_len: 0,
            funcs: [FuncSlot {
                name: 0,
                params: NIL,
                body: NIL,
                used: false,
            }; MAX_FUNCS],
            frames: [Frame { marker: 0 }; MAX_CALL_DEPTH],
            frame_len: 0,
            error: None,
            returning: None,
        }
    }

    fn fail(&mut self, msg: &'static str) -> Value {
        if self.error.is_none() {
            self.error = Some(msg);
        }
        Value::Nil
    }

    fn push_frame(&mut self) -> bool {
        if self.frame_len >= MAX_CALL_DEPTH {
            return false;
        }
        self.frames[self.frame_len] = Frame {
            marker: self.var_len,
        };
        self.frame_len += 1;
        true
    }

    fn pop_frame(&mut self) {
        if self.frame_len == 0 {
            return;
        }
        self.frame_len -= 1;
        let marker = self.frames[self.frame_len].marker;
        while self.var_len > marker {
            self.var_len -= 1;
            self.vars[self.var_len].used = false;
        }
    }

    fn set_var(&mut self, name: u16, value: Value) -> bool {
        // update existing in current frame first, then outer
        let start = if self.frame_len > 0 {
            self.frames[self.frame_len - 1].marker
        } else {
            0
        };
        for i in (start..self.var_len).rev() {
            if self.vars[i].used && self.vars[i].name == name {
                self.vars[i].value = value;
                return true;
            }
        }
        // outer scopes
        for i in (0..start).rev() {
            if self.vars[i].used && self.vars[i].name == name {
                self.vars[i].value = value;
                return true;
            }
        }
        if self.var_len >= MAX_VARS {
            return false;
        }
        self.vars[self.var_len] = VarSlot {
            name,
            value,
            used: true,
        };
        self.var_len += 1;
        true
    }

    fn get_var(&self, name: u16) -> Option<Value> {
        for i in (0..self.var_len).rev() {
            if self.vars[i].used && self.vars[i].name == name {
                return Some(self.vars[i].value);
            }
        }
        None
    }

    fn define_fun(&mut self, name: u16, params: NodeId, body: NodeId) -> bool {
        for i in 0..MAX_FUNCS {
            if self.funcs[i].used && self.funcs[i].name == name {
                self.funcs[i].params = params;
                self.funcs[i].body = body;
                return true;
            }
        }
        for i in 0..MAX_FUNCS {
            if !self.funcs[i].used {
                self.funcs[i] = FuncSlot {
                    name,
                    params,
                    body,
                    used: true,
                };
                return true;
            }
        }
        false
    }

    fn find_fun(&self, name: u16) -> Option<(NodeId, NodeId)> {
        for i in 0..MAX_FUNCS {
            if self.funcs[i].used && self.funcs[i].name == name {
                return Some((self.funcs[i].params, self.funcs[i].body));
            }
        }
        None
    }

    pub fn exec(&mut self, root: NodeId) -> Value {
        self.eval(root)
    }

    fn eval(&mut self, id: NodeId) -> Value {
        if id == NIL || self.error.is_some() {
            return Value::Nil;
        }
        if self.returning.is_some() {
            return Value::Nil;
        }
        let Some(node) = self.arena.get(id).copied() else {
            return self.fail("bad node");
        };
        match node {
            Node::Number(n) => Value::Int(n),
            Node::String(s) => Value::Str(s),
            Node::Bool(b) => Value::Bool(b),
            Node::Nil => Value::Nil,
            Node::Var(name) => self.get_var(name).unwrap_or(Value::Nil),
            Node::Let { name, value } | Node::Assign { name, value } => {
                let v = self.eval(value);
                if !self.set_var(name, v) {
                    return self.fail("too many variables");
                }
                v
            }
            Node::Unary { op, expr } => {
                let v = self.eval(expr);
                match op {
                    UnOp::Neg => match v.as_int() {
                        Some(n) => Value::Int(-n),
                        None => self.fail("unary - expects number"),
                    },
                    UnOp::Not => Value::Bool(!v.is_truthy()),
                }
            }
            Node::Binary { op, left, right } => self.eval_bin(op, left, right),
            Node::Print { args } => {
                self.print_list(args);
                purec::write("\n");
                Value::Nil
            }
            Node::If {
                cond,
                then_body,
                else_body,
            } => {
                if self.eval(cond).is_truthy() {
                    self.eval(then_body)
                } else if else_body != NIL {
                    self.eval(else_body)
                } else {
                    Value::Nil
                }
            }
            Node::While { cond, body } => {
                let mut last = Value::Nil;
                let mut guard = 0u32;
                while self.eval(cond).is_truthy() {
                    last = self.eval(body);
                    if self.returning.is_some() || self.error.is_some() {
                        break;
                    }
                    guard += 1;
                    if guard > 100_000 {
                        return self.fail("while: iteration limit");
                    }
                }
                last
            }
            Node::Fun {
                name,
                params,
                body,
            } => {
                if !self.define_fun(name, params, body) {
                    return self.fail("too many functions");
                }
                Value::Nil
            }
            Node::Return { value } => {
                let v = self.eval(value);
                self.returning = Some(v);
                v
            }
            Node::Block { stmts } => self.eval_list(stmts),
            Node::Call { name, args } => self.call(name, args),
            Node::List { .. } | Node::Param(_) => Value::Nil,
        }
    }

    fn eval_list(&mut self, mut cell: NodeId) -> Value {
        let mut last = Value::Nil;
        while cell != NIL {
            let Some(Node::List { head, next }) = self.arena.get(cell).copied() else {
                break;
            };
            last = self.eval(head);
            if self.returning.is_some() || self.error.is_some() {
                break;
            }
            cell = next;
        }
        last
    }

    fn print_list(&mut self, mut cell: NodeId) {
        let mut first = true;
        while cell != NIL {
            let Some(Node::List { head, next }) = self.arena.get(cell).copied() else {
                break;
            };
            if !first {
                purec::write(" ");
            }
            first = false;
            self.print_value(self.eval(head));
            cell = next;
        }
    }

    pub fn print_value(&self, v: Value) {
        match v {
            Value::Nil => purec::write("nil"),
            Value::Bool(true) => purec::write("true"),
            Value::Bool(false) => purec::write("false"),
            Value::Int(n) => {
                if n < 0 {
                    purec::write("-");
                    purec::write_u64((-n) as u64);
                } else {
                    purec::write_u64(n as u64);
                }
            }
            Value::Str(id) => purec::write(self.strings.get(id)),
        }
    }

    fn eval_bin(&mut self, op: BinOp, left: NodeId, right: NodeId) -> Value {
        match op {
            BinOp::And => {
                let l = self.eval(left);
                if !l.is_truthy() {
                    return l;
                }
                return self.eval(right);
            }
            BinOp::Or => {
                let l = self.eval(left);
                if l.is_truthy() {
                    return l;
                }
                return self.eval(right);
            }
            _ => {}
        }

        let l = self.eval(left);
        let r = self.eval(right);

        // string concat with +
        if matches!(op, BinOp::Add) {
            if let (Value::Str(a), Value::Str(b)) = (l, r) {
                return self.concat_str(a, b);
            }
        }

        match op {
            BinOp::Eq => Value::Bool(self.values_eq(l, r)),
            BinOp::Ne => Value::Bool(!self.values_eq(l, r)),
            BinOp::Lt | BinOp::Le | BinOp::Gt | BinOp::Ge | BinOp::Add | BinOp::Sub
            | BinOp::Mul | BinOp::Div | BinOp::Mod => {
                let (Some(a), Some(b)) = (l.as_int(), r.as_int()) else {
                    return self.fail("binary op expects numbers");
                };
                match op {
                    BinOp::Add => Value::Int(a.saturating_add(b)),
                    BinOp::Sub => Value::Int(a.saturating_sub(b)),
                    BinOp::Mul => Value::Int(a.saturating_mul(b)),
                    BinOp::Div => {
                        if b == 0 {
                            self.fail("division by zero")
                        } else {
                            Value::Int(a / b)
                        }
                    }
                    BinOp::Mod => {
                        if b == 0 {
                            self.fail("modulo by zero")
                        } else {
                            Value::Int(a % b)
                        }
                    }
                    BinOp::Lt => Value::Bool(a < b),
                    BinOp::Le => Value::Bool(a <= b),
                    BinOp::Gt => Value::Bool(a > b),
                    BinOp::Ge => Value::Bool(a >= b),
                    _ => Value::Nil,
                }
            }
            BinOp::And | BinOp::Or => unreachable_stub(),
        }
    }

    fn values_eq(&self, a: Value, b: Value) -> bool {
        match (a, b) {
            (Value::Nil, Value::Nil) => true,
            (Value::Bool(x), Value::Bool(y)) => x == y,
            (Value::Int(x), Value::Int(y)) => x == y,
            (Value::Str(x), Value::Str(y)) => self.strings.get(x) == self.strings.get(y),
            _ => false,
        }
    }

    fn concat_str(&mut self, a: u16, b: u16) -> Value {
        let sa = self.strings.get(a);
        let sb = self.strings.get(b);
        let mut buf = [0u8; STR_CAP];
        let mut n = 0;
        for &c in sa.as_bytes() {
            if n >= STR_CAP - 1 {
                break;
            }
            buf[n] = c;
            n += 1;
        }
        for &c in sb.as_bytes() {
            if n >= STR_CAP - 1 {
                break;
            }
            buf[n] = c;
            n += 1;
        }
        let s = core::str::from_utf8(&buf[..n]).unwrap_or("");
        match self.strings.intern(s) {
            Some(id) => Value::Str(id),
            None => self.fail("string pool full"),
        }
    }

    fn call(&mut self, name: u16, args: NodeId) -> Value {
        let Some((params, body)) = self.find_fun(name) else {
            return self.fail("undefined function");
        };

        // collect arg values
        let mut arg_vals = [Value::Nil; 8];
        let mut argc = 0usize;
        let mut cell = args;
        while cell != NIL && argc < 8 {
            let Some(Node::List { head, next }) = self.arena.get(cell).copied() else {
                break;
            };
            arg_vals[argc] = self.eval(head);
            argc += 1;
            cell = next;
        }

        if !self.push_frame() {
            return self.fail("call stack overflow");
        }

        // bind params
        let mut pcell = params;
        let mut i = 0;
        while pcell != NIL {
            let Some(Node::List { head, next }) = self.arena.get(pcell).copied() else {
                break;
            };
            if let Some(Node::Param(pname)) = self.arena.get(head).copied() {
                let v = if i < argc {
                    arg_vals[i]
                } else {
                    Value::Nil
                };
                if !self.set_var(pname, v) {
                    self.pop_frame();
                    return self.fail("too many variables");
                }
            }
            i += 1;
            pcell = next;
        }

        let _ = self.eval(body);
        let result = self.returning.take().unwrap_or(Value::Nil);
        self.pop_frame();
        result
    }
}

fn unreachable_stub() -> Value {
    Value::Nil
}
