/*
 * lexert.{cc,hh} -- configuration file parser for tools
 * Eddie Kohler
 *
 * Copyright (c) 1999-2000 Massachusetts Institute of Technology
 * Copyright (c) 2000 Mazu Networks, Inc.
 *
 * Permission is hereby granted, free of charge, to any person obtaining a
 * copy of this software and associated documentation files (the "Software"),
 * to deal in the Software without restriction, subject to the following
 * conditions:
 *
 * The above copyright notice and this permission notice shall be included in
 * all copies or substantial portions of the Software.
 *
 * Further elaboration of this license, including a DISCLAIMER OF ANY
 * WARRANTY, EXPRESS OR IMPLIED, is provided in the LICENSE file, which is
 * also accessible at http://www.pdos.lcs.mit.edu/click/license.html
 */

#ifdef HAVE_CONFIG_H
# include <config.h>
#endif
#include "lexert.hh"
#include "routert.hh"
#include <click/confparse.hh>
#include <ctype.h>
#include <stdlib.h>

LexerT::LexerT(ErrorHandler *errh, bool ignore_line_directives)
  : _data(0), _len(0), _pos(0), _lineno(1),
    _ignore_line_directives(ignore_line_directives),
    _tpos(0), _tfull(0), _router(0),
    _errh(errh)
{
  if (!_errh)
    _errh = ErrorHandler::default_handler();
  clear();
}

LexerT::~LexerT()
{
  clear();
}

void
LexerT::reset(const String &data, const String &filename)
{
  clear();
  
  _big_string = data;
  _data = _big_string.data();
  _len = _big_string.length();

  if (!filename)
    _filename = "line ";
  else if (filename.back() != ':' && !isspace(filename.back()))
    _filename = filename + ":";
  else
    _filename = filename;
  _original_filename = _filename;
  _lineno = 1;
}

void
LexerT::clear()
{
  if (_router)
    delete _router;
  _router = new RouterT;
  
  _big_string = "";
  // _data was freed by _big_string
  _data = 0;
  _len = 0;
  _pos = 0;
  _filename = "";
  _lineno = 0;
  _tpos = 0;
  _tfull = 0;
  
  _anonymous_offset = 0;
  _compound_depth = 0;
}

void
LexerT::set_router(RouterT *r)
{
  if (_router)
    delete _router;
  _router = r;
}


// LEXING: LOWEST LEVEL

unsigned
LexerT::skip_line(unsigned pos)
{
  _lineno++;
  for (; pos < _len; pos++)
    if (_data[pos] == '\n')
      return pos + 1;
    else if (_data[pos] == '\r') {
      if (pos < _len - 1 && _data[pos+1] == '\n')
	return pos + 2;
      else
	return pos + 1;
    }
  _lineno--;
  return _len;
}

unsigned
LexerT::skip_slash_star(unsigned pos)
{
  for (; pos < _len; pos++)
    if (_data[pos] == '\n')
      _lineno++;
    else if (_data[pos] == '\r') {
      if (pos < _len - 1 && _data[pos+1] == '\n') pos++;
      _lineno++;
    } else if (_data[pos] == '*' && pos < _len - 1 && _data[pos+1] == '/')
      return pos + 2;
  return _len;
}

unsigned
LexerT::skip_quote(unsigned pos, char endc)
{
  for (; pos < _len; pos++)
    if (_data[pos] == '\n')
      _lineno++;
    else if (_data[pos] == '\r') {
      if (pos < _len - 1 && _data[pos+1] == '\n') pos++;
      _lineno++;
    } else if (_data[pos] == '\\' && pos < _len - 1 && endc == '\"'
	       && _data[pos+1] == endc)
      pos++;
    else if (_data[pos] == endc)
      return pos + 1;
  return _len;
}

unsigned
LexerT::process_line_directive(unsigned pos)
{
  const char *data = _data;
  unsigned len = _len;
  
  for (pos++; pos < len && (data[pos] == ' ' || data[pos] == '\t'); pos++)
    /* nada */;
  if (pos < len - 4 && data[pos] == 'l' && data[pos+1] == 'i'
      && data[pos+2] == 'n' && data[pos+3] == 'e'
      && (data[pos+4] == ' ' || data[pos+4] == '\t')) {
    for (pos += 5; pos < len && (data[pos] == ' ' || data[pos] == '\t'); pos++)
      /* nada */;
  }
  if (pos >= len || !isdigit(data[pos])) {
    // complain about bad directive
    lerror("unknown preprocessor directive");
    return skip_line(pos);
  } else if (_ignore_line_directives)
    return skip_line(pos);
  
  // parse line number
  for (_lineno = 0; pos < len && isdigit(data[pos]); pos++)
    _lineno = _lineno * 10 + data[pos] - '0';
  _lineno--;			// account for extra line
  
  for (; pos < len && (data[pos] == ' ' || data[pos] == '\t'); pos++)
    /* nada */;
  if (pos < len && data[pos] == '\"') {
    // parse filename
    unsigned first_in_filename = pos;
    for (pos++; pos < len && data[pos] != '\"' && data[pos] != '\n' && data[pos] != '\r'; pos++)
      if (data[pos] == '\\' && pos < len - 1 && data[pos+1] != '\n' && data[pos+1] != '\r')
	pos++;
    _filename = cp_unquote(_big_string.substring(first_in_filename, pos - first_in_filename) + "\":");
    // an empty filename means return to the input file's name
    if (_filename == ":")
      _filename = _original_filename;
  }

  // reach end of line
  for (; pos < len && data[pos] != '\n' && data[pos] != '\r'; pos++)
    /* nada */;
  if (pos < len - 1 && data[pos] == '\r' && data[pos+1] == '\n')
    pos++;
  return pos;
}

Lexeme
LexerT::next_lexeme()
{
  unsigned pos = _pos;
  while (true) {
    while (pos < _len && isspace(_data[pos])) {
      if (_data[pos] == '\n')
	_lineno++;
      else if (_data[pos] == '\r') {
	if (pos < _len - 1 && _data[pos+1] == '\n') pos++;
	_lineno++;
      }
      pos++;
    }
    if (pos >= _len) {
      _pos = _len;
      return Lexeme();
    } else if (_data[pos] == '/' && pos < _len - 1) {
      if (_data[pos+1] == '/')
	pos = skip_line(pos + 2);
      else if (_data[pos+1] == '*')
	pos = skip_slash_star(pos + 2);
      else
	break;
    } else if (_data[pos] == '#' && (pos == 0 || _data[pos-1] == '\n' || _data[pos-1] == '\r'))
      pos = process_line_directive(pos);
    else
      break;
  }
  
  unsigned word_pos = pos;
  
  // find length of current word
  if (isalnum(_data[pos]) || _data[pos] == '_' || _data[pos] == '@') {
    pos++;
    while (pos < _len && (isalnum(_data[pos]) || _data[pos] == '_'
			  || _data[pos] == '/' || _data[pos] == '@')) {
      if (_data[pos] == '/' && pos < _len - 1
	  && (_data[pos+1] == '/' || _data[pos+1] == '*'))
	break;
      pos++;
    }
    _pos = pos;
    String word = _big_string.substring(word_pos, pos - word_pos);
    if (word.length() == 16 && word == "connectiontunnel")
      return Lexeme(lexTunnel, word);
    else if (word.length() == 12 && word == "elementclass")
      return Lexeme(lexElementclass, word);
    else if (word.length() == 7 && word == "require")
      return Lexeme(lexRequire, word);
    else
      return Lexeme(lexIdent, word);
  }

  // check for variable
  if (_data[pos] == '$') {
    pos++;
    while (pos < _len && (isalnum(_data[pos]) || _data[pos] == '_'))
      pos++;
    if (pos > word_pos + 1) {
      _pos = pos;
      return Lexeme(lexVariable, _big_string.substring(word_pos, pos - word_pos));
    } else
      pos--;
  }

  if (pos < _len - 1) {
    if (_data[pos] == '-' && _data[pos+1] == '>') {
      _pos = pos + 2;
      return Lexeme(lexArrow, _big_string.substring(word_pos, 2));
    } else if (_data[pos] == ':' && _data[pos+1] == ':') {
      _pos = pos + 2;
      return Lexeme(lex2Colon, _big_string.substring(word_pos, 2));
    } else if (_data[pos] == '|' && _data[pos+1] == '|') {
      _pos = pos + 2;
      return Lexeme(lex2Bar, _big_string.substring(word_pos, 2));
    }
  }
  if (pos < _len - 2 && _data[pos] == '.' && _data[pos+1] == '.' && _data[pos+2] == '.') {
    _pos = pos + 3;
    return Lexeme(lex3Dot, _big_string.substring(word_pos, 3));
  }
  
  _pos = pos + 1;
  return Lexeme(_data[word_pos], _big_string.substring(word_pos, 1));
}

String
LexerT::lex_config()
{
  unsigned config_pos = _pos;
  unsigned pos = _pos;
  unsigned paren_depth = 1;
  
  for (; pos < _len; pos++)
    if (_data[pos] == '(')
      paren_depth++;
    else if (_data[pos] == ')') {
      paren_depth--;
      if (!paren_depth) break;
    } else if (_data[pos] == '\n')
      _lineno++;
    else if (_data[pos] == '\r') {
      if (pos < _len - 1 && _data[pos+1] == '\n') pos++;
      _lineno++;
    } else if (_data[pos] == '/' && pos < _len - 1) {
      if (_data[pos+1] == '/')
	pos = skip_line(pos + 2) - 1;
      else if (_data[pos+1] == '*')
	pos = skip_slash_star(pos + 2) - 1;
    } else if (_data[pos] == '\'' || _data[pos] == '\"')
      pos = skip_quote(pos + 1, _data[pos]) - 1;
  
  _pos = pos;
  return _big_string.substring(config_pos, pos - config_pos);
}

String
LexerT::lexeme_string(int kind)
{
  char buf[12];
  if (kind == lexIdent)
    return "identifier";
  else if (kind == lexVariable)
    return "variable";
  else if (kind == lexArrow)
    return "`->'";
  else if (kind == lex2Colon)
    return "`::'";
  else if (kind == lex2Bar)
    return "`||'";
  else if (kind == lex3Dot)
    return "`...'";
  else if (kind == lexTunnel)
    return "`connectiontunnel'";
  else if (kind == lexElementclass)
    return "`elementclass'";
  else if (kind == lexRequire)
    return "`require'";
  else if (kind >= 32 && kind < 127) {
    sprintf(buf, "`%c'", kind);
    return buf;
  } else {
    sprintf(buf, "`\\%03d'", kind);
    return buf;
  }
}


// LEXING: MIDDLE LEVEL (WITH PUSHBACK)

const Lexeme &
LexerT::lex()
{
  int p = _tpos;
  if (_tpos == _tfull) {
    _tcircle[p] = next_lexeme();
    _tfull = (_tfull + 1) % TCIRCLE_SIZE;
  }
  _tpos = (_tpos + 1) % TCIRCLE_SIZE;
  return _tcircle[p];
}

void
LexerT::unlex(const Lexeme &t)
{
  _tcircle[_tfull] = t;
  _tfull = (_tfull + 1) % TCIRCLE_SIZE;
  assert(_tfull != _tpos);
}

bool
LexerT::expect(int kind, bool report_error = true)
{
  const Lexeme &t = lex();
  if (t.is(kind))
    return true;
  else {
    if (report_error)
      lerror("expected %s", lexeme_string(kind).cc());
    unlex(t);
    return false;
  }
}


// ERRORS

String
LexerT::landmark() const
{
  return _filename + String(_lineno);
}

int
LexerT::lerror(const char *format, ...)
{
  va_list val;
  va_start(val, format);
  _errh->verror(ErrorHandler::ERR_ERROR, landmark(), format, val);
  va_end(val);
  return -1;
}


// ELEMENT TYPES

int
LexerT::element_type(const String &s) const
{
  return _router->type_index(s);
}

int
LexerT::force_element_type(String s)
{
  if (_router->eindex(s) >= 0 && _router->type_index(s) < 0)
    lerror("`%s' was previously used as an element name", s.cc());
  return _router->get_type_index(s);
}


// ELEMENTS

String
LexerT::anon_element_name(const String &class_name) const
{
  String prefix = class_name + "@";
  int anonymizer = _router->nelements() - _anonymous_offset + 1;
  String name = prefix + String(anonymizer);
  while (_router->eindex(name) >= 0) {
    anonymizer++;
    name = prefix + String(anonymizer);
  }
  return name;
}

String
LexerT::anon_element_class_name(String prefix) const
{
  int anonymizer = _router->nelements() - _anonymous_offset + 1;
  String name = prefix + String(anonymizer);
  while (_router->type_index(name) >= 0) {
    anonymizer++;
    name = prefix + String(anonymizer);
  }
  return name;
}

int
LexerT::make_element(String name, int ftype, const String &conf,
		     const String &lm)
{
  return _router->get_eindex(name, ftype, conf, lm ? lm : landmark());
}

int
LexerT::make_anon_element(const String &class_name, int ftype,
			  const String &conf, const String &lm)
{
  return make_element(anon_element_name(class_name), ftype, conf, lm);
}

void
LexerT::connect(int element1, int port1, int port2, int element2)
{
  if (port1 < 0) port1 = 0;
  if (port2 < 0) port2 = 0;
  _router->add_connection(Hookup(element1, port1), Hookup(element2, port2),
			  landmark());
}


// PARSING

bool
LexerT::yport(int &port)
{
  const Lexeme &tlbrack = lex();
  if (!tlbrack.is('[')) {
    unlex(tlbrack);
    return false;
  }
  
  const Lexeme &tword = lex();
  if (tword.is(lexIdent)) {
    String p = tword.string();
    const char *ps = p.cc();
    if (isdigit(ps[0]) || ps[0] == '-')
      port = strtol(ps, (char **)&ps, 0);
    if (*ps != 0) {
      lerror("syntax error: port number should be integer");
      port = 0;
    }
    expect(']');
    return true;
  } else if (tword.is(']')) {
    lerror("syntax error: expected port number");
    port = 0;
    return true;
  } else {
    lerror("syntax error: expected port number");
    unlex(tword);
    return false;
  }
}

bool
LexerT::yelement(int &element, bool comma_ok)
{
  Lexeme t = lex();
  String name;
  int ftype;

  if (t.is(lexIdent)) {
    name = t.string();
    ftype = element_type(name);
  } else if (t.is('{')) {
    ftype = ycompound(String());
    name = _router->type_name(ftype);
  } else {
    unlex(t);
    return false;
  }
  
  String configuration, lm;
  const Lexeme &tparen = lex();
  if (tparen.is('(')) {
    lm = landmark();		// report landmark from before config string
    if (ftype < 0) ftype = force_element_type(name);
    configuration = lex_config();
    expect(')');
  } else
    unlex(tparen);
  
  if (ftype >= 0)
    element = make_anon_element(name, ftype, configuration, lm);
  else {
    const Lexeme &t2colon = lex();
    unlex(t2colon);
    if (t2colon.is(lex2Colon) || (t2colon.is(',') && comma_ok)) {
      ydeclaration(name);
      element = _router->eindex(name);
    } else {
      element = _router->eindex(name);
      if (element < 0) {
	// assume it's an element type
	ftype = force_element_type(name);
	element = make_anon_element(name, ftype, configuration, lm);
      }
    }
  }
  
  return true;
}

void
LexerT::ydeclaration(const String &first_element)
{
  Vector<String> decls;
  Lexeme t;
  
  if (first_element) {
    decls.push_back(first_element);
    goto midpoint;
  }
  
  while (true) {
    t = lex();
    if (!t.is(lexIdent))
      lerror("syntax error: expected element name");
    else
      decls.push_back(t.string());
    
   midpoint:
    const Lexeme &tsep = lex();
    if (tsep.is(','))
      /* do nothing */;
    else if (tsep.is(lex2Colon))
      break;
    else {
      lerror("syntax error: expected `::' or `,'");
      unlex(tsep);
      return;
    }
  }

  String lm = landmark();
  int ftype;
  t = lex();
  if (t.is(lexIdent))
    ftype = force_element_type(t.string());
  else if (t.is('{'))
    ftype = ycompound(String());
  else {
    lerror("missing element type in declaration");
    return;
  }
  
  String configuration;
  t = lex();
  if (t.is('(')) {
    configuration = lex_config();
    expect(')');
  } else
    unlex(t);
  
  for (int i = 0; i < decls.size(); i++) {
    String name = decls[i];
    int old_eidx = _router->eindex(name);
    if (old_eidx >= 0) {
      lerror("redeclaration of element `%s'", name.cc());
      _errh->lerror(_router->elandmark(old_eidx), "`%s' previously declared here", _router->edeclaration(old_eidx).cc());
    } else if (_router->type_index(name) >= 0)
      lerror("`%s' is an element class", name.cc());
    else
      make_element(name, ftype, configuration, lm);
  }
}

bool
LexerT::yconnection()
{
  int element1 = -1;
  int port1;
  Lexeme t;
  
  while (true) {
    int element2;
    int port2 = -1;
    
    // get element
    yport(port2);
    if (!yelement(element2, element1 < 0)) {
      if (port1 >= 0)
	lerror("output port useless at end of chain");
      return element1 >= 0;
    }
    
    if (element1 >= 0)
      connect(element1, port1, port2, element2);
    else if (port2 >= 0)
      lerror("input port useless at start of chain");
    
    port1 = -1;
    
   relex:
    t = lex();
    switch (t.kind()) {
      
     case ',':
     case lex2Colon:
      lerror("syntax error before `%s'", t.string().cc());
      goto relex;
      
     case lexArrow:
      break;
      
     case '[':
      unlex(t);
      yport(port1);
      goto relex;
      
     case lexIdent:
     case '{':
     case '}':
     case lex2Bar:
     case lexTunnel:
     case lexElementclass:
     case lexRequire:
      unlex(t);
      // FALLTHRU
     case ';':
     case lexEOF:
      if (port1 >= 0)
	lerror("output port useless at end of chain", port1);
      return true;
      
     default:
      lerror("syntax error near `%s'", String(t.string()).cc());
      if (t.kind() >= lexIdent)	// save meaningful tokens
	unlex(t);
      return true;
      
    }
    
    // have `x ->'
    element1 = element2;
  }
}

void
LexerT::yelementclass()
{
  Lexeme tname = lex();
  String facclass_name;
  if (!tname.is(lexIdent)) {
    unlex(tname);
    lerror("expected element type name");
  } else {
    String n = tname.string();
    if (_router->eindex(n) >= 0)
      lerror("`%s' already used as an element name", n.cc());
    else
      facclass_name = n;
  }

  Lexeme tnext = lex();
  if (tnext.is('{'))
    (void) ycompound(facclass_name);
    
  else if (tnext.is(lexIdent)) {
    ElementClassT *tclass = _router->type_class(tnext.string());
    if (facclass_name)
      _router->add_type_index(facclass_name, new SynonymElementClassT(tnext.string(), tclass));

  } else
    lerror("syntax error near `%s'", String(tnext.string()).cc());
}

void
LexerT::ytunnel()
{
  Lexeme tname1 = lex();
  if (!tname1.is(lexIdent)) {
    unlex(tname1);
    lerror("expected tunnel input name");
  }
  
  expect(lexArrow);
  
  Lexeme tname2 = lex();
  if (!tname2.is(lexIdent)) {
    unlex(tname2);
    lerror("expected tunnel output name");
  }
  
  if (tname1.is(lexIdent) && tname2.is(lexIdent))
    _router->add_tunnel(tname1.string(), tname2.string(), landmark(), _errh);
}

void
LexerT::ycompound_arguments(CompoundElementClassT *comptype)
{
  while (1) {
    const Lexeme &tvar = lex();
    if (!tvar.is(lexVariable)) {
      if (!tvar.is('|') || comptype->nformals() > 0)
	unlex(tvar);
      return;
    }
    comptype->add_formal(tvar.string());
    const Lexeme &tsep = lex();
    if (tsep.is('|'))
      return;
    else if (!tsep.is(',')) {
      lerror("expected `,' or `|'");
      unlex(tsep);
      return;
    }
  }
}

int
LexerT::ycompound(String name)
{
  bool anonymous = (name.length() == 0);
  if (anonymous)
    name = anon_element_class_name("@Class");
  
  // '{' was already read
  RouterT *old_router = _router;
  int old_offset = _anonymous_offset;
  ElementClassT *created = 0;

  // check for '...'
  const Lexeme &t = lex();
  if (t.is(lex3Dot)) {
    if (_router->type_index(name) < 0)
      _router->add_type_index(name, new SynonymElementClassT(name, 0));
    created = _router->type_class(name);
    expect(lex2Bar);
  } else
    unlex(t);

  ElementClassT *first = created;
  
  while (1) {
    _router = new RouterT(old_router);
    _router->get_eindex("input", RouterT::TUNNEL_TYPE, String(), landmark());
    _router->get_eindex("output", RouterT::TUNNEL_TYPE, String(), landmark());
    CompoundElementClassT *ct = new CompoundElementClassT(name, landmark(), _router, created, _compound_depth);
    _anonymous_offset = 2;
    _compound_depth++;

    ycompound_arguments(ct);
    while (ystatement(true))
      /* nada */;
    
    _compound_depth--;
    _anonymous_offset = old_offset;
    _router = old_router;
    
    ct->finish(_errh);
    created = ct;

    // check for '||' or '}'
    const Lexeme &t = lex();
    if (!t.is(lex2Bar))
      break;
  }

  created->cast_compound()->check_duplicates_until(first, _errh);
  
  if (anonymous)
    return old_router->get_anon_type_index(name, created);
  else
    return old_router->add_type_index(name, created);
}

void
LexerT::yrequire()
{
  if (expect('(')) {
    String requirement = lex_config();
    Vector<String> args;
    String word;
    cp_argvec(requirement, args);
    for (int i = 0; i < args.size(); i++) {
      if (!cp_word(args[i], &word))
	lerror("bad requirement: should be a single word");
      else
	_router->add_requirement(word);
    }
    expect(')');
  }
}

bool
LexerT::ystatement(bool nested)
{
  const Lexeme &t = lex();
  switch (t.kind()) {
    
   case lexIdent:
   case '[':
   case '{':
    unlex(t);
    yconnection();
    return true;
    
   case lexElementclass:
    yelementclass();
    return true;
    
   case lexTunnel:
    ytunnel();
    return true;

   case lexRequire:
    yrequire();
    return true;

   case ';':
    return true;
    
   case '}':
   case lex2Bar:
    if (!nested)
      goto syntax_error;
    unlex(t);
    return false;
    
   case lexEOF:
    if (nested)
      lerror("expected `}'");
    return false;
    
   default:
   syntax_error:
    lerror("syntax error near `%s'", String(t.string()).cc());
    return true;
    
  }
}


// COMPLETION

RouterT *
LexerT::take_router()
{
  RouterT *r = _router;
  _router = 0;
  return r;
}
