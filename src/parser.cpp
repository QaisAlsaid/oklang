#include "parser.hpp"
#include "ast.hpp"
#include "macros.hpp"
#include "operator.hpp"
#include "parsers.hpp"
#include "token.hpp"
#include "utility.hpp"
#include "vm_stack.hpp"
#include <memory>
#include <unordered_map>

namespace ok
{
  using prefix_parse_map = std::unordered_map<token_type, std::unique_ptr<prefix_parser_base>>;
  using infix_parse_map = std::unordered_map<token_type, std::unique_ptr<postfix_parser_base>>;

  static prefix_parse_map s_prefix_parse_map;
  static const auto _prefix_dummy = [] -> bool
  {
    s_prefix_parse_map.emplace(token_type::tok_identifier, std::make_unique<identifier_parser>());
    s_prefix_parse_map.emplace(token_type::tok_string, std::make_unique<string_parser>());
    s_prefix_parse_map.emplace(token_type::tok_number, std::make_unique<number_parser>());
    s_prefix_parse_map.emplace(token_type::tok_plus, std::make_unique<prefix_unary_parser>());
    s_prefix_parse_map.emplace(token_type::tok_minus, std::make_unique<prefix_unary_parser>());
    s_prefix_parse_map.emplace(token_type::tok_bang, std::make_unique<prefix_unary_parser>());
    s_prefix_parse_map.emplace(token_type::tok_plus_plus, std::make_unique<prefix_unary_parser>());
    s_prefix_parse_map.emplace(token_type::tok_minus_minus, std::make_unique<prefix_unary_parser>());
    s_prefix_parse_map.emplace(token_type::tok_left_paren, std::make_unique<group_parser>());
    s_prefix_parse_map.emplace(token_type::tok_true, std::make_unique<boolean_parser>());
    s_prefix_parse_map.emplace(token_type::tok_false, std::make_unique<boolean_parser>());
    s_prefix_parse_map.emplace(token_type::tok_null, std::make_unique<null_parser>());
    s_prefix_parse_map.emplace(token_type::tok_this, std::make_unique<this_parser>());
    s_prefix_parse_map.emplace(token_type::tok_super, std::make_unique<super_parser>());
    s_prefix_parse_map.emplace(token_type::tok_left_bracket, std::make_unique<array_parser>());
    s_prefix_parse_map.emplace(token_type::tok_left_brace, std::make_unique<map_parser>());
    return true;
  }();

  static infix_parse_map s_infix_parse_map;

  static const auto infix_dummy = [] -> bool
  {
    s_infix_parse_map.emplace(token_type::tok_left_paren, std::make_unique<call_parser>());
    s_infix_parse_map.emplace(token_type::tok_and, std::make_unique<infix_parser_binary>(precedence::prec_and, false));
    s_infix_parse_map.emplace(token_type::tok_or, std::make_unique<infix_parser_binary>(precedence::prec_or, false));
    s_infix_parse_map.emplace(token_type::tok_plus_plus,
                              std::make_unique<postfix_parser_unary>(precedence::prec_postfix));
    s_infix_parse_map.emplace(token_type::tok_minus_minus,
                              std::make_unique<postfix_parser_unary>(precedence::prec_postfix));
    s_infix_parse_map.emplace(token_type::tok_plus, std::make_unique<infix_parser_binary>(precedence::prec_sum, false));
    s_infix_parse_map.emplace(token_type::tok_minus,
                              std::make_unique<infix_parser_binary>(precedence::prec_sum, false));
    s_infix_parse_map.emplace(token_type::tok_asterisk,
                              std::make_unique<infix_parser_binary>(precedence::prec_product, false));
    s_infix_parse_map.emplace(token_type::tok_slash,
                              std::make_unique<infix_parser_binary>(precedence::prec_product, false));
    s_infix_parse_map.emplace(token_type::tok_modulo,
                              std::make_unique<infix_parser_binary>(precedence::prec_product, false));

    s_infix_parse_map.emplace(token_type::tok_bar,
                              std::make_unique<infix_parser_binary>(precedence::prec_bitwise_or, false));
    s_infix_parse_map.emplace(token_type::tok_caret,
                              std::make_unique<infix_parser_binary>(precedence::prec_bitwise_xor, false));
    s_infix_parse_map.emplace(token_type::tok_ampersand,
                              std::make_unique<infix_parser_binary>(precedence::prec_bitwise_and, false));

    s_infix_parse_map.emplace(token_type::tok_as, std::make_unique<infix_parser_binary>(precedence::prec_as, false));

    s_infix_parse_map.emplace(token_type::tok_shift_left,
                              std::make_unique<infix_parser_binary>(precedence::prec_shift, false));
    s_infix_parse_map.emplace(token_type::tok_shift_right,
                              std::make_unique<infix_parser_binary>(precedence::prec_shift, false));

    s_infix_parse_map.emplace(token_type::tok_question, std::make_unique<conditional_parser>());

    s_infix_parse_map.emplace(token_type::tok_assign, std::make_unique<assign_parser<ast::assign_expression>>());
    s_infix_parse_map.emplace(token_type::tok_plus_equal,
                              std::make_unique<infix_parser_binary>(precedence::prec_assignment, true));
    s_infix_parse_map.emplace(token_type::tok_minus_equal,
                              std::make_unique<infix_parser_binary>(precedence::prec_assignment, true));
    s_infix_parse_map.emplace(token_type::tok_asterisk_equal,
                              std::make_unique<infix_parser_binary>(precedence::prec_assignment, true));
    s_infix_parse_map.emplace(token_type::tok_slash_equal,
                              std::make_unique<infix_parser_binary>(precedence::prec_assignment, true));
    s_infix_parse_map.emplace(token_type::tok_modulo_equal,
                              std::make_unique<infix_parser_binary>(precedence::prec_assignment, true));
    s_infix_parse_map.emplace(token_type::tok_caret_equal,
                              std::make_unique<infix_parser_binary>(precedence::prec_assignment, true));
    s_infix_parse_map.emplace(token_type::tok_bar_equal,
                              std::make_unique<infix_parser_binary>(precedence::prec_assignment, true));
    s_infix_parse_map.emplace(token_type::tok_ampersand_equal,
                              std::make_unique<infix_parser_binary>(precedence::prec_assignment, true));
    s_infix_parse_map.emplace(token_type::tok_shift_left_equal,
                              std::make_unique<infix_parser_binary>(precedence::prec_assignment, true));
    s_infix_parse_map.emplace(token_type::tok_shift_right_equal,
                              std::make_unique<infix_parser_binary>(precedence::prec_assignment, true));

    s_infix_parse_map.emplace(token_type::tok_equal,
                              std::make_unique<infix_parser_binary>(precedence::prec_equality, false));
    s_infix_parse_map.emplace(token_type::tok_bang_equal,
                              std::make_unique<infix_parser_binary>(precedence::prec_equality, false));
    s_infix_parse_map.emplace(token_type::tok_less,
                              std::make_unique<infix_parser_binary>(precedence::prec_comparision, false));
    s_infix_parse_map.emplace(token_type::tok_less_equal,
                              std::make_unique<infix_parser_binary>(precedence::prec_comparision, false));
    s_infix_parse_map.emplace(token_type::tok_greater,
                              std::make_unique<infix_parser_binary>(precedence::prec_comparision, false));
    s_infix_parse_map.emplace(token_type::tok_greater_equal,
                              std::make_unique<infix_parser_binary>(precedence::prec_comparision, false));
    s_infix_parse_map.emplace(token_type::tok_dot, std::make_unique<access_parser>(precedence::prec_member, false));
    s_infix_parse_map.emplace(token_type::tok_left_bracket,
                              std::make_unique<subscript_parser>(precedence::prec_subscript, false));
    return true;
  }();

  parser::parser(token_array& p_token_array) : m_token_array(p_token_array)
  {
    advance();
  }

  // TODO(Qais): you pice of shit of a developer, check where this function returns,
  // and by that i mean the current token state, and does it guarantee it on all execution paths?
  // ok update, i might've gone too harsh on myself, while its clear(from the behavior of parse_expression_statement)
  // that it skips all tokens that are related to the current expression, but still is that a guarantee or just an
  // assumption?
  // TODO(Qais): i think this off by one problem is a common problem in parsers. so if you really want to design this
  // in an actually bullet proof/clean way, you might want to introduce something like advancement policy and state
  // synchronizer or santi checker or something like that idk tbh
  std::unique_ptr<ast::expression> parser::parse_expression(int p_precedence)
  {
    auto tok = current_token();
    auto prefix_it = s_prefix_parse_map.find(tok.type);
    if(s_prefix_parse_map.end() == prefix_it)
    {
      // TODO(Qais): wtf?
      parse_error(
          error::code::no_prefix_parse_function, "[{}] no prefix parser found for: {}", tok.offset, tok.raw_literal);
      return nullptr;
    }

    auto left = prefix_it->second->parse(*this, tok);

    auto lookahead = lookahead_token();
    while(lookahead.type != token_type::tok_semicolon && p_precedence < get_precedence(lookahead.type))
    {
      const auto infix_it = s_infix_parse_map.find(lookahead.type);
      if(s_infix_parse_map.end() == infix_it)
        return left;

      advance();
      tok = current_token();
      lookahead = lookahead_token();

      left = infix_it->second->parse(*this, tok, std::move(left));
      lookahead = lookahead_token();
    }
    return left;
  }

  std::unique_ptr<ast::program> parser::parse_program()
  {
    if(m_token_array.empty())
      return nullptr;
    auto tok = current_token();
    std::list<std::unique_ptr<ast::statement>> list;

    while(tok.type != token_type::tok_eof)
    {
      if(auto stmt = parse_declaration())
        list.push_back(std::move(stmt));
      advance();
      tok = current_token();
    }

    return std::make_unique<ast::program>(std::move(list));
  }

  std::unique_ptr<ast::statement> parser::parse_declaration()
  {
    std::unique_ptr<ast::statement> ret;
    const auto mods = parse_declaration_modifiers();

    switch(current_token().type)
    {
    case token_type::tok_let:
    {
      if((mods & ~let_declmods) != ast::declaration_modifier::dm_none)
      {
        parse_error(error::code::illegal_declaration_modifier, "illegal modifier in 'let' declaration");
      }
      ret = parse_let_declaration(mods);
      // if(current_token().type != token_type::tok_semicolon)
      //   parse_error(error::code::expected_token, "expected ';', after: {}", ret->token_literal());
      // munch_extra_semicolons();
      break;
    }
    case token_type::tok_fu:
    {
      if((mods & ~function_declmods) != ast::declaration_modifier::dm_none)
      {
        parse_error(error::code::illegal_declaration_modifier, "illegal modifier in 'fu' declaration");
      }
      ret = parse_function_declaration(mods);
      break;
    }
    case token_type::tok_class:
    {
      if((mods & ~class_declmods) != ast::declaration_modifier::dm_none)
      {
        parse_error(error::code::illegal_declaration_modifier, "illegal modifier in 'class' declaration");
      }
      ret = parse_class_declaration(mods);
      break;
    }
    default:
    {
      if(mods != ast::declaration_modifier::dm_none)
      {
        parse_error(error::code::illegal_declaration_modifier, "declaration modifiers can only apper in declarations");
      }
      ret = parse_statement();
      break;
    }
    }
    if(m_paranoia)
      sync_state();
    return ret;
  }

  ast::declaration_modifier parser::parse_declaration_modifiers()
  {
    ast::declaration_modifier mods = ast::declaration_modifier::dm_none;

    while(current_token().type != token_type::tok_eof)
    {
      const auto declmod = parse_declaration_modifier(current_token().type);
      if(declmod == ast::declaration_modifier::dm_none)
      {
        goto OUT;
      }
      else
      {
        mods |= declmod;
      }
      advance();
    }
  OUT:
    return mods;
  }

  ast::declaration_modifier parser::parse_declaration_modifier(token_type p_tok) const
  {
    switch(p_tok)
    {
    case token_type::tok_glob:
    {
      return ast::declaration_modifier::dm_global;
    }
    case token_type::tok_export:
    {
      return ast::declaration_modifier::dm_export;
    }
    case token_type::tok_static:
    {
      return ast::declaration_modifier::dm_static;
    }
    case token_type::tok_async:
    {
      return ast::declaration_modifier::dm_async;
    }
    default:
      return ast::declaration_modifier::dm_none;
    }
  }

  bool parser::is_declaration_modifier(token_type p_tok) const
  {
    return parse_declaration_modifier(p_tok) != ast::declaration_modifier::dm_none;
  }

  ast::binding_modifier parser::parse_binding_modifiers()
  {
    ast::binding_modifier mods = ast::binding_modifier::bm_none;

    while(current_token().type != token_type::tok_eof)
    {
      const auto bindmod = parse_binding_modifier(current_token().type);
      if(bindmod == ast::binding_modifier::bm_none)
      {
        goto OUT;
      }
      else
      {
        mods |= bindmod;
      }
      advance();
    }
  OUT:
    return mods;
  }

  ast::binding_modifier parser::parse_binding_modifier(token_type p_tok) const
  {
    switch(p_tok)
    {
    case token_type::tok_mut:
    {
      return ast::binding_modifier::bm_mut;
    }
    default:
      return ast::binding_modifier::bm_none;
    }
  }

  bool parser::is_binding_modifier(token_type p_tok) const
  {
    return parse_binding_modifier(p_tok) != ast::binding_modifier::bm_none;
  }

  std::unique_ptr<ast::statement> parser::parse_statement()
  {
    switch(current_token().type)
    {
    case token_type::tok_print:
      return parse_print_statement();
    case token_type::tok_left_brace:
      return parse_block_statement();
    case token_type::tok_if:
      return parse_if_statement();
    case token_type::tok_while:
      return parse_while_statement();
    case token_type::tok_for:
      return parse_for_statement();
    case token_type::tok_break:
    case token_type::tok_continue:
      return parse_control_flow_statement();
    case token_type::tok_return:
      return parse_return_statement();
    case token_type::tok_semicolon:
    {
      const auto semicolon = current_token();
      return std::make_unique<ast::empty_statement>(semicolon);
    }
    case token_type::tok_eof:
      return nullptr;
    default:
      return parse_expression_statement();
    }
  }

  std::unique_ptr<ast::expression_statement> parser::parse_expression_statement()
  {
    auto trigger_tok = current_token();
    auto expr = parse_expression();
    auto expr_stmt = std::make_unique<ast::expression_statement>(trigger_tok, std::move(expr));
    if(lookahead_token().type != token_type::tok_semicolon)
      parse_error(error::code::expected_token, "expected ';', after: {}", expr_stmt->token_literal());
    // munch_extra_semicolons();

    advance();
    return expr_stmt;
  }

  std::unique_ptr<ast::print_statement> parser::parse_print_statement()
  {
    auto print_tok = current_token();
    advance();
    auto expr = parse_expression();
    if(lookahead_token().type != token_type::tok_semicolon)
      parse_error(error::code::expected_token, "expected ';', after: {}", expr->token_literal());
    // munch_extra_semicolons();

    advance();

    return std::make_unique<ast::print_statement>(print_tok, std::move(expr));
  }

  std::unique_ptr<ast::control_flow_statement> parser::parse_control_flow_statement()
  {
    auto cf_tok = current_token();
    if(lookahead_token().type != token_type::tok_semicolon)
      parse_error(error::code::expected_token, "expected ';', after: {}", cf_tok.raw_literal);
    advance();
    return std::make_unique<ast::control_flow_statement>(cf_tok);
  }

  std::unique_ptr<ast::return_statement> parser::parse_return_statement()
  {
    auto ret_tok = current_token();
    std::unique_ptr<ast::expression> expr = nullptr;
    if(lookahead_token().type != token_type::tok_semicolon)
    {
      advance();
      expr = parse_expression();
    }
    if(lookahead_token().type != token_type::tok_semicolon)
      parse_error(error::code::expected_token, "expected ';', after return statement");
    advance();
    return std::make_unique<ast::return_statement>(ret_tok, std::move(expr));
  }

  std::unique_ptr<ast::block_statement> parser::parse_block_statement()
  {
    auto trigger_tok = current_token();
    std::list<std::unique_ptr<ast::statement>> statements;
    while(lookahead_token().type != token_type::tok_right_brace && lookahead_token().type != token_type::tok_eof)
    {
      advance();
      auto decl = parse_declaration();
      statements.push_back(std::move(decl));
    }
    advance();
    if(current_token().type != token_type::tok_right_brace)
      parse_error(error::code::expected_token, "expected '}}', after {}", statements.back()->token_literal());
    if(lookahead_token().type == token_type::tok_semicolon)
      advance(); // skip semicolon if present
    return std::make_unique<ast::block_statement>(trigger_tok, std::move(statements));
  }

  std::unique_ptr<ast::if_statement> parser::parse_if_statement()
  {
    auto if_token = current_token();
    advance();
    auto expr = parse_expression();
    if(!expr)
    {
      parse_error(error::code::invalid_expression, "expected expression after 'if'");
      return nullptr;
    }
    advance();
    if(current_token().type != token_type::tok_arrow)
    {
      parse_error(error::code::expected_token, "expected '->' after expression: {}", expr->to_string());
      return nullptr;
    }
    advance(); // skip '->'
    auto cons = parse_statement();
    if(!cons)
    {
      parse_error(error::code::expected_statement, "expected statement after '{}'", "->");
      return nullptr;
    }
    std::unique_ptr<ast::statement> alt = nullptr;
    auto lat = lookahead_token().type;
    if(lat == token_type::tok_else)
    {
      advance();
      advance();
      if(current_token().type == token_type::tok_arrow)
        advance();
      alt = parse_statement();
      if(alt == nullptr)
      {
        parse_error(error::code::expected_statement, "expected statement after 'else'");
        return nullptr;
      }
    }
    return std::make_unique<ast::if_statement>(if_token, std::move(expr), std::move(cons), std::move(alt));
  }

  std::unique_ptr<ast::while_statement> parser::parse_while_statement()
  {
    auto while_token = current_token();
    advance();
    auto expr = parse_expression();
    if(!expr)
    {
      parse_error(error::code::invalid_expression, "expected expression after 'while'");
      return nullptr;
    }
    advance();
    if(current_token().type != token_type::tok_arrow)
    {
      parse_error(error::code::expected_token, "expected '->' after expression: {}", expr->to_string());
      return nullptr;
    }
    advance(); // skip '->'
    auto body = parse_statement();
    if(!body)
    {
      parse_error(error::code::expected_statement, "expected statement after '{}'", "->");
      return nullptr;
    }
    return std::make_unique<ast::while_statement>(while_token, std::move(expr), std::move(body));
  }

  std::unique_ptr<ast::for_statement> parser::parse_for_statement()
  {
    auto for_token = current_token();
    std::unique_ptr<ast::statement> body;
    std::unique_ptr<ast::statement> init = nullptr;
    std::unique_ptr<ast::expression> cond = nullptr;
    std::unique_ptr<ast::expression> inc = nullptr;
    advance();
    if(current_token().type == token_type::tok_semicolon)
    {
      // stays nullptr
    }
    else if(is_declaration_modifier(current_token().type))
    {
      // for better error messages!
      const auto mods = parse_declaration_modifiers();
      if(lookahead_token().type != token_type::tok_let)
      {
        parse_error(error::code::expected_token, "expected 'let' after declaration modifiers in for initializer");
      }
      advance();
      {
        parse_error(error::code::illegal_declaration_modifier,
                    "illegal usage of declaration modifiers in for loop initializer");
      }
      init = parse_let_declaration(mods); // checks semicolon
    }
    else if(current_token().type == token_type::tok_let)
    {
      init = parse_let_declaration(ast::declaration_modifier::dm_none); // checks semicolon
    }
    else
    {
      init = parse_expression_statement(); // checks semicolon
    }
    advance();
    if(current_token().type != token_type::tok_semicolon) // there must be a condition
    {
      cond = parse_expression();
      if(cond == nullptr)
      {
        parse_error(error::code::expected_expression, "expected expression as 'for' condition");
        return nullptr;
      }
      advance();
    }
    if(current_token().type != token_type::tok_semicolon)
    {
      parse_error(error::code::expected_token, "expected ';' after condition");
      return nullptr;
    }
    advance();
    if(current_token().type != token_type::tok_arrow) // there must be an increment
    {
      inc = parse_expression();
      if(inc == nullptr)
      {
        parse_error(error::code::expected_expression, "expected expression as 'for' increment");
        return nullptr;
      }
      advance();
    }
    if(current_token().type != token_type::tok_arrow)
    {
      parse_error(error::code::expected_token, "expected '->' after 'for' clauses");
      return nullptr;
    }
    advance();
    body = parse_statement();
    if(body == nullptr)
    {
      parse_error(error::code::expected_statement, "expected statement as 'for' body");
      return nullptr;
    }
    return std::make_unique<ast::for_statement>(
        for_token, std::move(body), std::move(init), std::move(cond), std::move(inc));
  }

  std::unique_ptr<ast::let_declaration> parser::parse_let_declaration(ast::declaration_modifier p_declmods)
  {
    auto let_tok = current_token();
    advance();
    const auto bindmods = parse_binding_modifiers();
    if((bindmods & ~let_bindmods) != ast::binding_modifier::bm_none)
    {
      parse_error(error::code::illegal_binding_modifier, "illegal binding modifiers in let declaration binding");
    }

    std::unique_ptr<ast::binding> binding;
    std::unique_ptr<ast::expression> value = std::make_unique<ast::null_expression>(let_tok);

    auto expr = parse_expression();
    if(expr->get_type() == ast::node_type::nt_assign_expr)
    {
      auto assign = std::unique_ptr<ast::assign_expression>((ast::assign_expression*)expr.release());
      auto& left = assign->get_left();
      value = std::move(assign->get_right());
      if(left->get_type() != ast::node_type::nt_identifier_expr)
      {
        parse_error(error::code::expected_identifier, "expected identifier in let declaration");
      }
      auto left_ident = (ast::identifier_expression*)left.release();
      binding = std::make_unique<ast::binding>(left_ident->get_token(), left_ident->get_value(), bindmods);
    }
    else if(expr->get_type() == ast::node_type::nt_identifier_expr)
    {
      auto ident = (ast::identifier_expression*)expr.release();
      binding = std::make_unique<ast::binding>(ident->get_token(), ident->get_value(), bindmods);
    }
    else
      parse_error(error::code::expected_identifier, "expected identifier in let declaration");

    advance();
    if(current_token().type != token_type::tok_semicolon)
      parse_error(error::code::expected_token, "expected ';', after: let declaration");

    return std::make_unique<ast::let_declaration>(let_tok, std::move(binding), std::move(value), p_declmods);
  } // glob let inst_1 = cls(); glob let inst_2 = cls(); inst_1.value = 69; inst_2.value = 1;

  std::unique_ptr<ast::function_declaration> parser::parse_function_declaration(ast::declaration_modifier p_mods)
  {
    // fu do_stuff() -> {  } // optional arrow when body is block
    // fu do_stuff() {  } // notmal stuff
    // fu do_stuff() -> print("doing stuff"); // one statement requires arrow
    //// let f = fu () -> {  } // TODO, Update: no cant do this because it breaks the semantics and is not expected to
    //// work when the functions are statements rather than expressions
    auto fu_token = current_token();
    advance();
    return parse_function_declaration_impl(fu_token, p_mods, function_bindmods, "function");
  }

  std::unique_ptr<ast::function_declaration>
  parser::parse_function_declaration_impl(token p_trigger,
                                          ast::declaration_modifier p_mods,
                                          ast::binding_modifier p_allowed_binding_mods,
                                          std::string_view callit,
                                          unique_overridable_operator_type p_allowed_overrides,
                                          unique_overridable_operator_type* p_out_op_type)
  {
    std::unique_ptr<ast::binding> binding;
    const auto mods = parse_binding_modifiers();
    token tok;
    if(current_token().type == token_type::tok_identifier)
    {
      tok = current_token();
    }
    else if(current_token().type == token_type::tok_operator)
    {
      advance();
      tok = current_token();
      unique_overridable_operator_type uoot = unique_overridable_operator_type::uoot_none;
      if(unique_overridable_operator_type_from_string(tok.raw_literal).second != 0)
      {
        auto ootp = unique_overridable_operator_type_from_string(tok.raw_literal);
        uoot = ootp.first;
        if(ootp.second == 2)
        {
          advance();
          advance();
          token_type expect;
          if(ootp.first == unique_overridable_operator_type::uoot_unary_postfix_call)
          {
            expect = token_type::tok_right_paren;
          }
          else
          {
            expect = token_type::tok_right_bracket;
          }

          if(lookahead_token().type != expect)
          {
            parse_error(error::code::expected_token,
                        "expected '{}', in operator overload, in {} declaration",
                        token_type_to_string(expect),
                        callit);
          }
          else
          {
            advance();
            tok.raw_literal += current_token().raw_literal;
          }
        }
      }
      else if(current_token().type == token_type::tok_identifier)
      {
        uoot = unique_overridable_operator_type::uoot_convert;
      }
      if(p_out_op_type)
        *p_out_op_type = uoot;

      if(!is_in_group(uoot, p_allowed_overrides))
      {
        parse_error(error::code::illegal_operator_override,
                    "illegal operator overload '{}', in {} declaration",
                    tok.raw_literal,
                    callit);
      }
    }

    if((mods & ~p_allowed_binding_mods) != ast::binding_modifier::bm_none)
    {
      parse_error(error::code::illegal_binding_modifier, "illegal binding modifiers in {} declaration binding", callit);
    }
    binding = std::make_unique<ast::binding>(tok, tok.raw_literal, mods);
    advance();

    // else keep it null, because mods doesnt affect anything, compiler will worn about this situation
    if(current_token().type != token_type::tok_left_paren)
      parse_error(error::code::expected_token, "expected '(' in {} declaration", callit);
    advance();
    std::list<std::unique_ptr<ast::binding>> params;
    if(current_token().type != token_type::tok_right_paren)
    {
      while(current_token().type != token_type::tok_right_paren && current_token().type != token_type::tok_eof)
      {
        const auto mods = parse_binding_modifiers();
        if((mods & ~function_param_bindmods) != ast::binding_modifier::bm_none)
        {
          parse_error(error::code::illegal_binding_modifier,
                      "illegal binding modifiers in {} parameters declaration binding",
                      callit);
        }
        auto tok = current_token();
        advance();
        params.push_back(std::make_unique<ast::binding>(tok, tok.raw_literal, mods));
        if(current_token().type == token_type::tok_comma)
        {
          advance();
        }
        else if(current_token().type != token_type::tok_right_paren)
        {
          parse_error(parser::error::code::expected_token, "expected ')', after parameters list");
          break;
        }
      }
      if(current_token().type != token_type::tok_right_paren) // eof
      {
        parse_error(parser::error::code::expected_token, "expected ')', after parameters list");
      }
      else
      {
        advance();
      }

      // const auto mods = parse_binding_modifiers();
      // if((mods & ~function_param_bindmods) != ast::binding_modifier::bm_none)
      // {
      //   parse_error(error::code::illegal_binding_modifier,
      //               "illegal binding modifiers in {} parameters declaration binding",
      //               callit);
      // }
      // auto tok = current_token();
      // if(tok.type != token_type::tok_identifier)
      // {
      //   parse_error(error::code::expected_identifier, "expected identifier as {} parameter", callit);
      // }
      // params.push_back(std::make_unique<ast::binding>(tok, tok.raw_literal(), mods));
      // advance();
      // while(current_token().type == token_type::tok_comma)
      // {
      //   advance();
      //   advance();
      //   const auto mods = parse_binding_modifiers();
      //   if((mods & ~function_param_bindmods) != ast::binding_modifier::bm_none)
      //   {
      //     parse_error(error::code::illegal_binding_modifier,
      //                 "illegal binding modifiers in {} parameters declaration binding",
      //                 callit);
      //   }
      //   auto tok = current_token();
      //   if(tok.type != token_type::tok_identifier)
      //   {
      //     parse_error(error::code::expected_identifier, "expected identifier as {} parameter", callit);
      //   }
      //   params.push_back(std::make_unique<ast::binding>(tok, tok.raw_literal(), mods));
      // }
      // auto tok = current_token();
      // params.push_back(std::make_unique<ast::identifier_expression>(tok, tok.raw_literal));
      // while(lookahead_token().type == token_type::tok_comma)
      // {
      //   advance();
      //   advance();
      //   auto tok = current_token();
      //   if(tok.type != token_type::tok_identifier)
      //   {
      //     parse_error(error::code::expected_identifier, "expected identifier as {} parameter", callit);
      //   }
      //   params.push_back(std::make_unique<ast::identifier_expression>(tok, tok.raw_literal));
      // }
      // advance();
    }
    else
    {
      advance();
    }

    // if(current_token().type != token_type::tok_right_paren)
    //   parse_error(error::code::expected_token, "expected ')' after {} parameters", callit);

    // if next is arrow we skip and set had arrow to true, if next is brace we dont do anything, but if next is other
    // stateent we check arrow first if not we error if its there we dont do anything and parse normally

    bool had_arrow = false;
    if(current_token().type == token_type::tok_arrow)
    {
      advance();
      had_arrow = true;
    }

    if(current_token().type != token_type::tok_left_brace && !had_arrow)
      parse_error(error::code::expected_token, "expected '->' in one-liner {} declaration", callit);

    // advance();

    auto body = parse_statement();

    return std::make_unique<ast::function_declaration>(
        p_trigger, std::move(body), std::move(binding), std::move(params), p_mods);
  }

  std::unique_ptr<ast::class_declaration> parser::parse_class_declaration(ast::declaration_modifier p_mods)
  {
    auto cls_token = current_token();
    advance();
    if(current_token().type != token_type::tok_identifier)
    {
      parse_error(error::code::expected_identifier, "expected class name");
    }
    const auto mods = parse_binding_modifiers();
    if((mods & ~class_bindmods) != ast::binding_modifier::bm_none)
    {
      parse_error(error::code::illegal_binding_modifier, "illegal binding modifiers in class declaration binding");
    }
    auto tok = current_token();
    advance();
    auto binding = std::make_unique<ast::binding>(tok, tok.raw_literal, mods);
    std::unique_ptr<ast::identifier_expression> super = nullptr;
    // advance();

    if(current_token().type == token_type::tok_inherits)
    {
      if(lookahead_token().type != token_type::tok_identifier)
      {
        parse_error(error::code::expected_identifier, "expected superclass name");
      }
      advance();
      auto curr = current_token();
      super = std::make_unique<ast::identifier_expression>(curr, curr.raw_literal);
      advance();
    }

    if(current_token().type != token_type::tok_left_brace)
    {
      parse_error(error::code::expected_token, "expected '{{' before class body");
    }
    advance();

    std::list<ast::class_declaration::method_declaration> methods;
    while(current_token().type != token_type::tok_right_brace && current_token().type != token_type::tok_eof)
    {
      const auto mods = parse_declaration_modifiers();
      auto tok = current_token();
      switch(tok.type)
      {
      case token_type::tok_fu:
      {
        if((mods & ~method_declmods) != ast::declaration_modifier::dm_none)
        {
          parse_error(error::code::illegal_declaration_modifier, "illegal modifier in 'method' declaration");
        }
        auto op = unique_overridable_operator_type::uoot_method;
        advance();
        auto md = parse_function_declaration_impl(
            tok, ast::declaration_modifier::dm_none, ast::binding_modifier::bm_none, "method");
        if(md->get_binding()->get_name() == "ctor")
          op = unique_overridable_operator_type::uoot_ctor;
        else if(md->get_binding()->get_name() == "dtor")
          op = unique_overridable_operator_type::uoot_dtor;
        methods.push_back(ast::class_declaration::method_declaration{std::move(md), op});
        advance(); // the function's right brace
        break;
      }
      case token_type::tok_let:
      {
        if((mods & ~field_declmods) != ast::declaration_modifier::dm_none)
        {
          parse_error(error::code::illegal_declaration_modifier, "illegal modifier in 'field' declaration");
        }
        // TODO(Qais):
        break;
      }
      case token_type::tok_operator:
      {
        if((mods & ~method_declmods) != ast::declaration_modifier::dm_none)
        {
          parse_error(error::code::illegal_declaration_modifier, "illegal modifier in 'method' declaration");
        }
        methods.push_back(parse_operator_overload(mods));
        break;
      }
      default:
        parse_error(error::code::unexpected_token, "unexpected token: '{}' in class body", tok.raw_literal);
        goto OUT;
      }
    }
  OUT:
    if(current_token().type != token_type::tok_right_brace)
    {
      parse_error(error::code::expected_token, "expected '}}' after class body");
    }
    // advance();

    return std::make_unique<ast::class_declaration>(
        cls_token, std::move(binding), std::move(methods), std::move(super), p_mods);
  }

  ast::class_declaration::method_declaration parser::parse_operator_overload(ast::declaration_modifier p_dm)
  {
    unique_overridable_operator_type op;
    auto fd = parse_function_declaration_impl(current_token(),
                                              p_dm,
                                              ast::binding_modifier::bm_none,
                                              "method",
                                              unique_overridable_operator_type::uoot_group_all_operator |
                                                  unique_overridable_operator_type::uoot_convert,
                                              &op);
    advance(); // }

    return {std::move(fd), op};
  }

  bool parser::advance()
  {
    if(m_lookahead_token >= m_token_array.size() - 1)
    {
      m_current_token = m_lookahead_token;
      return false; // cant advance, no more tokens!
    }
    m_current_token = m_lookahead_token++;
    return true;
  }

  bool parser::expect_next(token_type p_type)
  {
    if(lookahead_token().type == p_type)
      return advance();

    parse_error(error::code::expected_token,
                "expected token of type: {}, at: {}",
                token_type_to_string(p_type),
                lookahead_token().offset);
    return false;
  }

  int parser::get_precedence(token_type p_type)
  {
    auto infix_it = s_infix_parse_map.find(p_type);
    if(s_infix_parse_map.end() != infix_it)
      return infix_it->second->get_precedence();
    return precedence::prec_lowest;
  }

  token parser::current_token() const
  {
    return m_token_array[m_current_token];
  }

  token parser::lookahead_token() const
  {
    return m_token_array[m_lookahead_token];
  }

  void parser::sync_state()
  {
    m_paranoia = false;
    while(lookahead_token().type != token_type::tok_eof)
    {
      if(current_token().type == token_type::tok_semicolon)
      {
        munch_extra_semicolons();
        return;
      }
      switch(current_token().type)
      {
      case token_type::tok_class:
      case token_type::tok_fu:
      case token_type::tok_let:
      case token_type::tok_for:
      case token_type::tok_if:
      case token_type::tok_while:
      case token_type::tok_print:
      case token_type::tok_return:
        return;
      default:;
      }
      advance();
    }
  }

  auto parser::get_errors() const -> const errors&
  {
    return m_errors;
  }

  void parser::munch_extra_semicolons()
  {
    // while(lookahead_token().type == token_type::tok_semicolon)
    //{
    //   advance();
    // }
  }

  void parser::errors::show() const
  {
    for(const auto& err : errs)
      ERRORLN("parse error: {}:{}", error::code_to_string(err.error_code), err.message);
  }

} // namespace ok