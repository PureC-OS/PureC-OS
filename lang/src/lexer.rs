//! Токены PureL.

#[derive(Clone, Copy, PartialEq, Eq)]
pub enum TokenKind {
    Eof,
    Number,
    String,
    Ident,
    // keywords
    Let,
    If,
    Else,
    While,
    Fun,
    Return,
    True,
    False,
    Nil,
    Print,
    And,
    Or,
    Not,
    End,
    Then,
    Do,
    // punctuation
    Plus,
    Minus,
    Star,
    Slash,
    Percent,
    EqEq,
    BangEq,
    Lt,
    LtEq,
    Gt,
    GtEq,
    Assign,
    LParen,
    RParen,
    Comma,
    Newline,
}

#[derive(Clone, Copy)]
pub struct Token {
    pub kind: TokenKind,
    pub start: usize,
    pub len: usize,
}

impl Token {
    pub fn slice<'a>(&self, src: &'a str) -> &'a str {
        &src[self.start..self.start + self.len]
    }
}

pub struct Lexer<'a> {
    src: &'a [u8],
    pos: usize,
}

impl<'a> Lexer<'a> {
    pub fn new(src: &'a str) -> Self {
        Self {
            src: src.as_bytes(),
            pos: 0,
        }
    }

    fn peek(&self) -> Option<u8> {
        self.src.get(self.pos).copied()
    }

    fn bump(&mut self) -> Option<u8> {
        let c = self.peek()?;
        self.pos += 1;
        Some(c)
    }

    fn skip_spaces(&mut self) {
        while let Some(c) = self.peek() {
            if c == b' ' || c == b'\t' || c == b'\r' {
                self.pos += 1;
            } else if c == b'#' {
                while let Some(c) = self.peek() {
                    if c == b'\n' {
                        break;
                    }
                    self.pos += 1;
                }
            } else {
                break;
            }
        }
    }

    pub fn next_token(&mut self) -> Token {
        self.skip_spaces();
        let start = self.pos;
        let Some(c) = self.bump() else {
            return Token {
                kind: TokenKind::Eof,
                start,
                len: 0,
            };
        };

        match c {
            b'\n' => Token {
                kind: TokenKind::Newline,
                start,
                len: 1,
            },
            b'+' => Token {
                kind: TokenKind::Plus,
                start,
                len: 1,
            },
            b'-' => Token {
                kind: TokenKind::Minus,
                start,
                len: 1,
            },
            b'*' => Token {
                kind: TokenKind::Star,
                start,
                len: 1,
            },
            b'/' => Token {
                kind: TokenKind::Slash,
                start,
                len: 1,
            },
            b'%' => Token {
                kind: TokenKind::Percent,
                start,
                len: 1,
            },
            b'(' => Token {
                kind: TokenKind::LParen,
                start,
                len: 1,
            },
            b')' => Token {
                kind: TokenKind::RParen,
                start,
                len: 1,
            },
            b',' => Token {
                kind: TokenKind::Comma,
                start,
                len: 1,
            },
            b'=' => {
                if self.peek() == Some(b'=') {
                    self.bump();
                    Token {
                        kind: TokenKind::EqEq,
                        start,
                        len: 2,
                    }
                } else {
                    Token {
                        kind: TokenKind::Assign,
                        start,
                        len: 1,
                    }
                }
            }
            b'!' => {
                if self.peek() == Some(b'=') {
                    self.bump();
                    Token {
                        kind: TokenKind::BangEq,
                        start,
                        len: 2,
                    }
                } else {
                    Token {
                        kind: TokenKind::Not,
                        start,
                        len: 1,
                    }
                }
            }
            b'<' => {
                if self.peek() == Some(b'=') {
                    self.bump();
                    Token {
                        kind: TokenKind::LtEq,
                        start,
                        len: 2,
                    }
                } else {
                    Token {
                        kind: TokenKind::Lt,
                        start,
                        len: 1,
                    }
                }
            }
            b'>' => {
                if self.peek() == Some(b'=') {
                    self.bump();
                    Token {
                        kind: TokenKind::GtEq,
                        start,
                        len: 2,
                    }
                } else {
                    Token {
                        kind: TokenKind::Gt,
                        start,
                        len: 1,
                    }
                }
            }
            b'"' => self.string(start),
            b'0'..=b'9' => self.number(start),
            b'a'..=b'z' | b'A'..=b'Z' | b'_' => self.ident(start),
            _ => Token {
                kind: TokenKind::Eof,
                start,
                len: 0,
            },
        }
    }

    fn number(&mut self, start: usize) -> Token {
        while matches!(self.peek(), Some(b'0'..=b'9')) {
            self.bump();
        }
        Token {
            kind: TokenKind::Number,
            start,
            len: self.pos - start,
        }
    }

    fn string(&mut self, start: usize) -> Token {
        while let Some(c) = self.bump() {
            if c == b'"' {
                break;
            }
            if c == b'\\' {
                let _ = self.bump();
            }
        }
        Token {
            kind: TokenKind::String,
            start,
            len: self.pos - start,
        }
    }

    fn ident(&mut self, start: usize) -> Token {
        while matches!(
            self.peek(),
            Some(b'a'..=b'z' | b'A'..=b'Z' | b'0'..=b'9' | b'_')
        ) {
            self.bump();
        }
        let len = self.pos - start;
        let text = core::str::from_utf8(&self.src[start..start + len]).unwrap_or("");
        let kind = match text {
            "let" => TokenKind::Let,
            "if" => TokenKind::If,
            "else" => TokenKind::Else,
            "while" => TokenKind::While,
            "fun" => TokenKind::Fun,
            "return" => TokenKind::Return,
            "true" => TokenKind::True,
            "false" => TokenKind::False,
            "nil" => TokenKind::Nil,
            "print" => TokenKind::Print,
            "and" => TokenKind::And,
            "or" => TokenKind::Or,
            "not" => TokenKind::Not,
            "end" => TokenKind::End,
            "then" => TokenKind::Then,
            "do" => TokenKind::Do,
            _ => TokenKind::Ident,
        };
        Token { kind, start, len }
    }
}
