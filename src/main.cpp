#include "chunk.hpp"
#include "lexer.hpp"
#include "parser.hpp"
#include "token.hpp"
#include "vm.hpp"
#include <print>

// TODO(Qais): the most basic logging solution will be sufficient (compile time switch!)
//  also a basic testing header will be great, and write some tests!

struct tests_progress
{
  tests_progress(uint32_t t, uint32_t p, uint32_t f) : total(t), pass(p), fail(f)
  {
  }
  uint32_t total, pass, fail;
};
static tests_progress test(const std::string_view src, const std::string_view expect);

int main(int argc, char** argv)
{
  ok::chunk chunk;
  // auto constant_idx = chunk.add_constant(69);
  // chunk.write(ok::opcode::op_constant, 22);
  // chunk.write(constant_idx, 123);
  // chunk.write(ok::opcode::op_return, 123);
  // for(auto i = 0; i < UINT8_MAX + UINT8_MAX; ++i)
  //  chunk.write_constant(i, i);
  // chunk.write(ok::opcode::op_return, 22);

  // chunk.write_constant(3.14, 123);
  // chunk.write_constant(3.76, 123);
  // chunk.write(ok::opcode::op_add, 123);

  // chunk.write_constant(10, 123);
  // chunk.write(ok::opcode::op_divide, 123);
  // chunk.write(ok::opcode::op_negate, 123);
  // chunk.write(ok::opcode::op_negate, 123);
  // chunk.write(ok::opcode::op_return, 123);
  // ok::vm vm;
  // vm.interpret(&chunk);
  //  ok::debug::disassembler::disassemble_chunk(chunk, "test");

  // ok::lexer lx;
  // auto arr = lx.lex("(5/(5+5))");
  // ok::parser parser{arr};
  // auto program = parser.parse_program();
  // if(program == nullptr)
  //   std::println(stderr, "program is nullptr!");
  // for(const auto& err : parser.get_errors())
  //   std::println(stderr, "{}", err.message);

  // std::println("{}", program->to_string());

  // for(auto idx = 0; auto elem : arr)
  //   std::println("found elem: type: {}, raw: {}, line: {}, at in array location: {}",
  //                ok::token_type_to_string(elem.type),
  //                elem.raw_literal,
  //                elem.line,
  //                idx++);

  // test("a", "a");
  // test("15", "15");
  // test("a()", "a()");
  // test("a(b)", "a(b)");
  // test("a(a, b)", "a(a, b)");
  // test("a(b)(c)", "a(b)(c)");
  // test("a(b) + c(d)", "(a(b)+c(d))");
  // test("a(b ? c : d, e + f)", "a((b?c:d), (e+f))");
  // test("-a", "(-a)");
  // test("-!a", "(-(!a))");
  // test("-a * b", "((-a)*b)");
  // test("!a + b", "((!a)+b)");
  // test("a = b + c * e - f / g", "(a=((b+(c*e))-(f/g)))");
  // test("a = b = c", "(a=(b=c))");
  // test("a + b - c", "((a+b)-c)");
  // test("a * b / c", "((a*b)/c)");
  // test("a ? b : c ? d : e", "(a?b:(c?d:e))");
  // test("a + b ? c * d : e / f", "((a+b)?(c*d):(e/f))");
  // test("a ? b ? c : d : e", "(a?(b?c:d):e)");
  // auto tests_stats = test("a + (b + c) + d", "((a+(b+c))+d)");
  // std::println("total tests: {}", tests_stats.total);
  // std::println("passed: {}", tests_stats.pass);
  // std::println("failed: {}", tests_stats.fail);
  // std::println("accuracy: {}%", (float)tests_stats.pass / (float)tests_stats.total * 100);

  ok::vm vm;
  // TODO(Qais): if you'd do something like "let a = 'a\na'"  the \n is not being handled by oklang's runtime, rather
  // its the c++ compiler, so if i were to take this string from a file this wont work!

  // TODO(Qais): parsing malformed assignments like this: "a * b = c" indeed doesnt parse, but it doesnt propagate an
  // error either.
  // the problem comes from the individual expression parsers doesnt propagate errors, so fix that asap

  vm.interpret("let f; {f=2; f=69; print f; { f='qais'; print f; }}");
}

static tests_progress test(const std::string_view src, const std::string_view expect)
{
  ok::lexer lx;
  auto arr = lx.lex(src);
  ok::parser prs{arr};
  auto pro = prs.parse_program();
  std::string out;
  if(pro == nullptr)
    out = "null";
  else
    out = pro->to_string();

  static uint32_t tests_pass = 0, tests_fail = 0, tests_tot = 0;
  tests_tot++;
  bool pass = false;
  if(out == expect)
  {
    tests_pass++;
    pass = true;
  }
  else
    tests_fail++;

  std::println("test[{}]: ({}), actual: '{}', expected: '{}'", tests_tot, pass ? "pass" : "fail", out, expect);
  const auto& errs = prs.get_errors();
  if(!errs.empty())
  {
    std::println("extras: errors: {{");
    for(auto num = 0; const auto& err : errs)
      std::println("  error[{}]: '{}'", num++, err.message);
    std::println("}}");
  }
  return {tests_tot, tests_pass, tests_fail};
}