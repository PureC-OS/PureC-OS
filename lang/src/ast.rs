//! AST на индексах (без кучи).

pub type NodeId = u16;

pub const NIL: NodeId = u16::MAX;

#[derive(Clone, Copy)]
pub enum BinOp {
    Add,
    Sub,
    Mul,
    Div,
    Mod,
    Eq,
    Ne,
    Lt,
    Le,
    Gt,
    Ge,
    And,
    Or,
}

#[derive(Clone, Copy)]
pub enum UnOp {
    Neg,
    Not,
}

#[derive(Clone, Copy)]
pub enum Node {
    /// i64 literal
    Number(i64),
    /// индекс в string pool
    String(u16),
    Bool(bool),
    Nil,
    /// имя → string pool
    Var(u16),
    Assign {
        name: u16,
        value: NodeId,
    },
    Let {
        name: u16,
        value: NodeId,
    },
    Unary {
        op: UnOp,
        expr: NodeId,
    },
    Binary {
        op: BinOp,
        left: NodeId,
        right: NodeId,
    },
    Call {
        name: u16,
        args: NodeId,
    },
    /// связанный список аргументов / statements
    List {
        head: NodeId,
        next: NodeId,
    },
    Print {
        args: NodeId,
    },
    If {
        cond: NodeId,
        then_body: NodeId,
        else_body: NodeId,
    },
    While {
        cond: NodeId,
        body: NodeId,
    },
    Fun {
        name: u16,
        params: NodeId,
        body: NodeId,
    },
    Return {
        value: NodeId,
    },
    Block {
        stmts: NodeId,
    },
    /// имя параметра в списке
    Param(u16),
}

pub struct Arena {
    nodes: [Node; MAX_NODES],
    len: usize,
}

pub const MAX_NODES: usize = 512;
pub const MAX_STRINGS: usize = 128;
pub const MAX_STR_BYTES: usize = 48;

impl Arena {
    pub const fn new() -> Self {
        Self {
            nodes: [Node::Nil; MAX_NODES],
            len: 0,
        }
    }

    pub fn clear(&mut self) {
        self.len = 0;
    }

    pub fn alloc(&mut self, node: Node) -> Option<NodeId> {
        if self.len >= MAX_NODES {
            return None;
        }
        let id = self.len as NodeId;
        self.nodes[self.len] = node;
        self.len += 1;
        Some(id)
    }

    pub fn get(&self, id: NodeId) -> Option<&Node> {
        if id == NIL || (id as usize) >= self.len {
            None
        } else {
            Some(&self.nodes[id as usize])
        }
    }

    pub fn node_count(&self) -> usize {
        self.len
    }

    pub fn node_mut(&mut self, id: NodeId) -> &mut Node {
        &mut self.nodes[id as usize]
    }
}

pub struct StringPool {
    data: [[u8; MAX_STR_BYTES]; MAX_STRINGS],
    lens: [u8; MAX_STRINGS],
    len: usize,
}

impl StringPool {
    pub const fn new() -> Self {
        Self {
            data: [[0; MAX_STR_BYTES]; MAX_STRINGS],
            lens: [0; MAX_STRINGS],
            len: 0,
        }
    }

    pub fn clear(&mut self) {
        self.len = 0;
    }

    pub fn intern(&mut self, s: &str) -> Option<u16> {
        let bytes = s.as_bytes();
        if bytes.len() >= MAX_STR_BYTES {
            return None;
        }
        for i in 0..self.len {
            let n = self.lens[i] as usize;
            if n == bytes.len() && &self.data[i][..n] == bytes {
                return Some(i as u16);
            }
        }
        if self.len >= MAX_STRINGS {
            return None;
        }
        let i = self.len;
        self.data[i][..bytes.len()].copy_from_slice(bytes);
        self.lens[i] = bytes.len() as u8;
        self.len += 1;
        Some(i as u16)
    }

    pub fn get(&self, id: u16) -> &str {
        let i = id as usize;
        if i >= self.len {
            return "";
        }
        let n = self.lens[i] as usize;
        core::str::from_utf8(&self.data[i][..n]).unwrap_or("")
    }
}
