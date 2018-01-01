#include <cstring>
#include <vector>

#include "ast/ast.h"
#include "base/source.h"
#include "error_log.h"
#include "ir/ir.h"
#include "scope.h"
#include "type/type.h"
#include "util/command_line_args.h"
#include "util/timer.h"
#include "base/debug.h"

Timer timer;

#define CHECK_FOR_ERRORS                                                       \
  do {                                                                         \
    if (ErrorLog::NumErrors() != 0) {                                          \
      ErrorLog::Dump();                                                        \
      return -1;                                                               \
    }                                                                          \
  } while (false)

extern void ReplEval(AST::Expression *expr);

extern std::vector<base::owned_ptr<AST::Statements>> ParseAllFiles();
extern Timer timer;

base::owned_ptr<AST::Statements> global_statements;

int GenerateCode() {
  auto stmts_by_file = ParseAllFiles();

  CHECK_FOR_ERRORS;

  RUN(timer, "AST Setup") {
    global_statements = AST::Statements::Merge(std::move(stmts_by_file));
    global_statements->assign_scope(Scope::Global);
  }

  RUN(timer, "Verify and Emit") {
    for (auto& stmt : global_statements->statements) {
      if (!stmt->is<AST::Declaration>()) { continue; }
      auto *decl = &stmt->as<AST::Declaration>();
      decl->EmitIR(IR::Cmd::Kind::Exec);
    }
  }

  RUN(timer, "Verify preconditions") {
    std::queue<IR::Func *> validation_queue;
    for (const auto &fn : IR::Func::All) { validation_queue.push(fn.get()); }

    int num_errors = 0;
    while (!validation_queue.empty()) {
      auto fn = std::move(validation_queue.front());
      validation_queue.pop();
      num_errors += fn->ValidateCalls(&validation_queue);
    }
  }

  return 0;
}

int RunRepl() {
  std::puts("Icarus REPL (v0.1)");

  Repl repl;
  while (true) {
    auto stmts = repl.Parse();
    for (auto &stmt : stmts->statements) {
      if (stmt->is<AST::Declaration>()) {
        auto *decl = &stmt->as<AST::Declaration>();
        decl->assign_scope(Scope::Global);
        decl->EmitIR(IR::Cmd::Kind::Exec);

      } else if (stmt->is<AST::Expression>()) {
        auto *expr = &stmt->as<AST::Expression>();
        expr->assign_scope(Scope::Global);
        ReplEval(expr);
        fprintf(stderr, "\n");
      } else {
        NOT_YET(*stmt);
      }
    }
  }
  return 0;
}

int main(int argc, char *argv[]) {
#ifdef DEBUG
  signal(SIGABRT, debug::DumpStackTrace);
#endif

  RUN(timer, "Argument parsing") {
    switch (ParseCLArguments(argc, argv)) {
      case CLArgFlag::QuitSuccessfully: return 0;
      case CLArgFlag::QuitWithFailure: return -1;
      case CLArgFlag::Continue:;
    }
  }

  return repl ? RunRepl() : GenerateCode();
}
