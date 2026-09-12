//! Рекурсивный спуск → Arena.

use crate::ast::{Arena, BinOp, NIL, Node, NodeId, StringPool, UnOp};
use crate::lexer::{Lexer, Token, TokenKind};

pub struct Parser<'a> {
    src: &'a str,
    lexer: Lexer<'a>,
    cur: Token,
    pub arena: &'a mut Arena,
    pub strings: &'a mut StringPool,
    pub error: Option<&'static str>,
}

impl<'a> Parser<'a> {
    pub fn new(src: &'a str, arena: &'a mut Arena, strings: &'a mut StringPool) -> Self {
        let mut lexer = Lexer::new(src);
        let cur = lexer.next_token();
        Self {
            src,
            lexer,
            cur,
            arena,
            strings,
            error: None,
        }
    }

    fn bump(&mut self) {
        self.cur = self.lexer.next_token();
    }

    fn skip_nl(&mut self) {
        while self.cur.kind == TokenKind::Newline {
            self.bump();
        }
    }

    fn fail(&mut self, msg: &'static str) -> NodeId {
        if self.error.is_none() {
            self.error = Some(msg);
        }
        NIL
    }

    fn expect(&mut self, kind: TokenKind, msg: &'static str) -> bool {
        if self.cur.kind == kind {
            self.bump();
            true
        } else {
            self.fail(msg);
            false
        }
    }

    fn intern_cur(&mut self) -> Option<u16> {
        let text = self.cur.slice(self.src);
        self.strings.intern(text)
    }

    pub fn parse_program(&mut self) -> NodeId {
        self.skip_nl();
        let mut head = NIL;
        let mut tail = NIL;
        while self.cur.kind != TokenKind::Eof && self.error.is_none() {
            let stmt = self.parse_stmt();
            if stmt == NIL && self.error.is_some() {
                break;
            }
            if stmt == NIL {
                self.skip_nl();
                continue;
            }
            let cell = match self.arena.alloc(Node::List {
                head: stmt,
                next: NIL,
            }) {
                Some(id) => id,
                None => return self.fail("out of nodes"),
            };
            if head == NIL {
                head = cell;
            } else if let Some(Node::List { next, .. }) = self.arena.get(tail).copied() {
                // need mutable update — re-write via internal access
                let _ = next;
                self.set_list_next(tail, cell);
            }
            tail = cell;
            self.skip_nl();
        }
        match self.arena.alloc(Node::Block { stmts: head }) {
            Some(id) => id,
            None => self.fail("out of nodes"),
        }
    }

    fn set_list_next(&mut self, id: NodeId, next: NodeId) {
        if let Some(node) = self.arena_get_mut(id) {
            if let Node::List { head, .. } = *node {
                *node = Node::List { head, next };
            }
        }
    }

    fn arena_get_mut(&mut self, id: NodeId) -> Option<&mut Node> {
        if id == NIL || (id as usize) >= self.arena.len_nodes() {
            None
        } else {
            Some(self.arena.get_mut(id))
        }
    }

    fn parse_stmt(&mut self) -> NodeId {
        self.skip_nl();
        match self.cur.kind {
            TokenKind::Let => self.parse_let(),
            TokenKind::If => self.parse_if(),
            TokenKind::While => self.parse_while(),
            TokenKind::Fun => self.parse_fun(),
            TokenKind::Return => {
                self.bump();
                let value = if matches!(
                    self.cur.kind,
                    TokenKind::Newline | TokenKind::Eof | TokenKind::End | TokenKind::Else
                ) {
                    match self.arena.alloc(Node::Nil) {
                        Some(id) => id,
                        None => return self.fail("out of nodes"),
                    }
                } else {
                    self.parse_expr()
                };
                match self.arena.alloc(Node::Return { value }) {
                    Some(id) => id,
                    None => self.fail("out of nodes"),
                }
            }
            TokenKind::Print => self.parse_print(),
            TokenKind::Ident => {
                // assign or expr-stmt
                let name_tok = self.cur;
                self.bump();
                if self.cur.kind == TokenKind::Assign {
                    self.bump();
                    let Some(name) = self.strings.intern(name_tok.slice(self.src)) else {
                        return self.fail("string pool full");
                    };
                    let value = self.parse_expr();
                    match self.arena.alloc(Node::Assign { name, value }) {
                        Some(id) => id,
                        None => self.fail("out of nodes"),
                    }
                } else {
                    // put ident back conceptually: parse as call/expr starting with var
                    // We already consumed ident — rebuild as Var and continue if '('
                    let Some(name) = self.strings.intern(name_tok.slice(self.src)) else {
                        return self.fail("string pool full");
                    };
                    let var = match self.arena.alloc(Node::Var(name)) {
                        Some(id) => id,
                        None => return self.fail("out of nodes"),
                    };
                    if self.cur.kind == TokenKind::LParen {
                        self.parse_call_tail(name)
                    } else {
                        // bare expression starting with var — allow binary continuation
                        self.parse_expr_continue(var, 0)
                    }
                }
            }
            TokenKind::Eof | TokenKind::End | TokenKind::Else => NIL,
            _ => {
                // expression statement
                self.parse_expr()
            }
        }
    }

    fn parse_let(&mut self) -> NodeId {
        self.bump(); // let
        if self.cur.kind != TokenKind::Ident {
            return self.fail("expected name after let");
        }
        let Some(name) = self.intern_cur() else {
            return self.fail("string pool full");
        };
        self.bump();
        if !self.expect(TokenKind::Assign, "expected '=' after let name") {
            return NIL;
        }
        let value = self.parse_expr();
        match self.arena.alloc(Node::Let { name, value }) {
            Some(id) => id,
            None => self.fail("out of nodes"),
        }
    }

    fn parse_print(&mut self) -> NodeId {
        self.bump();
        let args = self.parse_arg_list(false);
        match self.arena.alloc(Node::Print { args }) {
            Some(id) => id,
            None => self.fail("out of nodes"),
        }
    }

    fn parse_if(&mut self) -> NodeId {
        self.bump(); // if
        let cond = self.parse_expr();
        let _ = self.expect(TokenKind::Then, "expected 'then'");
        self.skip_nl();
        let then_body = self.parse_block_until(&[TokenKind::Else, TokenKind::End]);
        let else_body = if self.cur.kind == TokenKind::Else {
            self.bump();
            self.skip_nl();
            self.parse_block_until(&[TokenKind::End])
        } else {
            NIL
        };
        let _ = self.expect(TokenKind::End, "expected 'end'");
        match self.arena.alloc(Node::If {
            cond,
            then_body,
            else_body,
        }) {
            Some(id) => id,
            None => self.fail("out of nodes"),
        }
    }

    fn parse_while(&mut self) -> NodeId {
        self.bump();
        let cond = self.parse_expr();
        let _ = self.expect(TokenKind::Do, "expected 'do'");
        self.skip_nl();
        let body = self.parse_block_until(&[TokenKind::End]);
        let _ = self.expect(TokenKind::End, "expected 'end'");
        match self.arena.alloc(Node::While { cond, body }) {
            Some(id) => id,
            None => self.fail("out of nodes"),
        }
    }

    fn parse_fun(&mut self) -> NodeId {
        self.bump();
        if self.cur.kind != TokenKind::Ident {
            return self.fail("expected function name");
        }
        let Some(name) = self.intern_cur() else {
            return self.fail("string pool full");
        };
        self.bump();
        if !self.expect(TokenKind::LParen, "expected '('") {
            return NIL;
        }
        let mut params = NIL;
        let mut tail = NIL;
        if self.cur.kind != TokenKind::RParen {
            loop {
                if self.cur.kind != TokenKind::Ident {
                    return self.fail("expected parameter");
                }
                let Some(pname) = self.intern_cur() else {
                    return self.fail("string pool full");
                };
                self.bump();
                let pnode = match self.arena.alloc(Node::Param(pname)) {
                    Some(id) => id,
                    None => return self.fail("out of nodes"),
                };
                let cell = match self.arena.alloc(Node::List {
                    head: pnode,
                    next: NIL,
                }) {
                    Some(id) => id,
                    None => return self.fail("out of nodes"),
                };
                if params == NIL {
                    params = cell;
                } else {
                    self.set_list_next(tail, cell);
                }
                tail = cell;
                if self.cur.kind == TokenKind::Comma {
                    self.bump();
                    continue;
                }
                break;
            }
        }
        if !self.expect(TokenKind::RParen, "expected ')'") {
            return NIL;
        }
        self.skip_nl();
        let body = self.parse_block_until(&[TokenKind::End]);
        let _ = self.expect(TokenKind::End, "expected 'end'");
        match self.arena.alloc(Node::Fun {
            name,
            params,
            body,
        }) {
            Some(id) => id,
            None => self.fail("out of nodes"),
        }
    }

    fn parse_block_until(&mut self, stops: &[TokenKind]) -> NodeId {
        let mut head = NIL;
        let mut tail = NIL;
        loop {
            self.skip_nl();
            if self.cur.kind == TokenKind::Eof || stops.contains(&self.cur.kind) {
                break;
            }
            let stmt = self.parse_stmt();
            if stmt == NIL {
                break;
            }
            let cell = match self.arena.alloc(Node::List {
                head: stmt,
                next: NIL,
            }) {
                Some(id) => id,
                None => return self.fail("out of nodes"),
            };
            if head == NIL {
                head = cell;
            } else {
                self.set_list_next(tail, cell);
            }
            tail = cell;
        }
        match self.arena.alloc(Node::Block { stmts: head }) {
            Some(id) => id,
            None => self.fail("out of nodes"),
        }
    }

    fn parse_arg_list(&mut self, require_paren: bool) -> NodeId {
        let mut has_paren = false;
        if self.cur.kind == TokenKind::LParen {
            has_paren = true;
            self.bump();
        } else if require_paren {
            return self.fail("expected '('");
        }

        let mut head = NIL;
        let mut tail = NIL;

        let empty = matches!(
            self.cur.kind,
            TokenKind::RParen
                | TokenKind::Newline
                | TokenKind::Eof
                | TokenKind::End
                | TokenKind::Else
        );

        if !empty {
            loop {
                let expr = self.parse_expr();
                let cell = match self.arena.alloc(Node::List {
                    head: expr,
                    next: NIL,
                }) {
                    Some(id) => id,
                    None => return self.fail("out of nodes"),
                };
                if head == NIL {
                    head = cell;
                } else {
                    self.set_list_next(tail, cell);
                }
                tail = cell;
                if self.cur.kind == TokenKind::Comma {
                    self.bump();
                    continue;
                }
                break;
            }
        }

        if has_paren {
            let _ = self.expect(TokenKind::RParen, "expected ')'");
        }
        head
    }

    fn parse_call_tail(&mut self, name: u16) -> NodeId {
        let args = self.parse_arg_list(true);
        match self.arena.alloc(Node::Call { name, args }) {
            Some(id) => id,
            None => self.fail("out of nodes"),
        }
    }

    // Pratt parser
    fn parse_expr(&mut self) -> NodeId {
        self.parse_expr_bp(0)
    }

    fn parse_expr_continue(&mut self, left: NodeId, min_bp: u8) -> NodeId {
        let mut left = left;
        loop {
            let Some((op, l_bp, r_bp)) = Self::binop_of(self.cur.kind) else {
                break;
            };
            if l_bp < min_bp {
                break;
            }
            self.bump();
            let right = self.parse_expr_bp(r_bp);
            left = match self.arena.alloc(Node::Binary { op, left, right }) {
                Some(id) => id,
                None => return self.fail("out of nodes"),
            };
        }
        left
    }

    fn parse_expr_bp(&mut self, min_bp: u8) -> NodeId {
        let mut left = self.parse_prefix();
        if left == NIL {
            return NIL;
        }
        left = self.parse_expr_continue(left, min_bp);
        left
    }

    fn parse_prefix(&mut self) -> NodeId {
        match self.cur.kind {
            TokenKind::Number => {
                let text = self.cur.slice(self.src);
                let mut n: i64 = 0;
                for b in text.bytes() {
                    n = n.saturating_mul(10).saturating_add((b - b'0') as i64);
                }
                self.bump();
                match self.arena.alloc(Node::Number(n)) {
                    Some(id) => id,
                    None => self.fail("out of nodes"),
                }
            }
            TokenKind::String => {
                let raw = self.cur.slice(self.src);
                // strip quotes
                let inner = if raw.len() >= 2 {
                    &raw[1..raw.len() - 1]
                } else {
                    ""
                };
                let Some(id) = self.strings.intern(inner) else {
                    return self.fail("string pool full");
                };
                self.bump();
                match self.arena.alloc(Node::String(id)) {
                    Some(nid) => nid,
                    None => self.fail("out of nodes"),
                }
            }
            TokenKind::True => {
                self.bump();
                match self.arena.alloc(Node::Bool(true)) {
                    Some(id) => id,
                    None => self.fail("out of nodes"),
                }
            }
            TokenKind::False => {
                self.bump();
                match self.arena.alloc(Node::Bool(false)) {
                    Some(id) => id,
                    None => self.fail("out of nodes"),
                }
            }
            TokenKind::Nil => {
                self.bump();
                match self.arena.alloc(Node::Nil) {
                    Some(id) => id,
                    None => self.fail("out of nodes"),
                }
            }
            TokenKind::Ident => {
                let Some(name) = self.intern_cur() else {
                    return self.fail("string pool full");
                };
                self.bump();
                if self.cur.kind == TokenKind::LParen {
                    self.parse_call_tail(name)
                } else {
                    match self.arena.alloc(Node::Var(name)) {
                        Some(id) => id,
                        None => self.fail("out of nodes"),
                    }
                }
            }
            TokenKind::Minus => {
                self.bump();
                let expr = self.parse_prefix();
                match self.arena.alloc(Node::Unary {
                    op: UnOp::Neg,
                    expr,
                }) {
                    Some(id) => id,
                    None => self.fail("out of nodes"),
                }
            }
            TokenKind::Not => {
                self.bump();
                let expr = self.parse_prefix();
                match self.arena.alloc(Node::Unary {
                    op: UnOp::Not,
                    expr,
                }) {
                    Some(id) => id,
                    None => self.fail("out of nodes"),
                }
            }
            TokenKind::LParen => {
                self.bump();
                let e = self.parse_expr();
                let _ = self.expect(TokenKind::RParen, "expected ')'");
                e
            }
            _ => self.fail("unexpected token in expression"),
        }
    }

    fn binop_of(kind: TokenKind) -> Option<(BinOp, u8, u8)> {
        // (op, left_bp, right_bp)
        match kind {
            TokenKind::Or => Some((BinOp::Or, 1, 2)),
            TokenKind::And => Some((BinOp::And, 3, 4)),
            TokenKind::EqEq => Some((BinOp::Eq, 5, 6)),
            TokenKind::BangEq => Some((BinOp::Ne, 5, 6)),
            TokenKind::Lt => Some((BinOp::Lt, 7, 8)),
            TokenKind::LtEq => Some((BinOp::Le, 7, 8)),
            TokenKind::Gt => Some((BinOp::Gt, 7, 8)),
            TokenKind::GtEq => Some((BinOp::Ge, 7, 8)),
            TokenKind::Plus => Some((BinOp::Add, 9, 10)),
            TokenKind::Minus => Some((BinOp::Sub, 9, 10)),
            TokenKind::Star => Some((BinOp::Mul, 11, 12)),
            TokenKind::Slash => Some((BinOp::Div, 11, 12)),
            TokenKind::Percent => Some((BinOp::Mod, 11, 12)),
            _ => None,
        }
    }
}

// helpers on Arena
impl Arena {
    pub fn len_nodes(&self) -> usize {
        // expose via method — need field access
        self.node_count()
    }

    pub fn get_mut(&mut self, id: NodeId) -> &mut Node {
        self.node_mut(id)
    }
}
