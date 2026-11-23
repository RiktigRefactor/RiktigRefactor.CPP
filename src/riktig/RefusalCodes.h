#pragma once

// The source of truth for these codes and their meanings is refusal-codes.md.
namespace riktig::refusal_codes {

inline constexpr const char *WorkspaceDoesNotCompile = "workspace-does-not-compile";
inline constexpr const char *InvalidLocation = "invalid-location";
inline constexpr const char *NoSymbol = "no-symbol";
inline constexpr const char *AmbiguousSymbol = "ambiguous-symbol";
inline constexpr const char *NotRenameable = "not-renameable";
inline constexpr const char *InvalidIdentifier = "invalid-identifier";
inline constexpr const char *ConflictingDeclaration = "conflicting-declaration";
inline constexpr const char *WouldIntroduceErrors = "would-introduce-errors";
inline constexpr const char *BindingWouldChange = "binding-would-change";
inline constexpr const char *MacroInvolved = "macro-involved";
inline constexpr const char *PreprocessorRegion = "preprocessor-region";
inline constexpr const char *UnsupportedReference = "unsupported-reference";
inline constexpr const char *ReferenceOutsideWorkspace = "reference-outside-workspace";

inline constexpr const char *InvalidSelection = "invalid-selection";
inline constexpr const char *UnsupportedContext = "unsupported-context";
inline constexpr const char *NonLocalControlFlow = "non-local-control-flow";
inline constexpr const char *ContainsCoroutineKeyword = "contains-coroutine-keyword";
inline constexpr const char *TypeNotNameable = "type-not-nameable";
inline constexpr const char *TargetTypedExpression = "target-typed-expression";
inline constexpr const char *TemporaryLifetime = "temporary-lifetime";
inline constexpr const char *DestructorTiming = "destructor-timing";
inline constexpr const char *DeclarationEscapes = "declaration-escapes";
inline constexpr const char *CapturedVariable = "captured-variable";
inline constexpr const char *FunctionNameObserved = "function-name-observed";
inline constexpr const char *UnevaluatedOperand = "unevaluated-operand";
inline constexpr const char *BitField = "bit-field";

inline constexpr const char *NotAnInstanceMethod = "not-an-instance-method";
inline constexpr const char *VirtualDispatch = "virtual-dispatch";
inline constexpr const char *TemplateInvolved = "template-involved";
inline constexpr const char *UnsupportedMethod = "unsupported-method";
inline constexpr const char *MemberFunctionPointer = "member-function-pointer";
inline constexpr const char *ReceiverNotLvalue = "receiver-not-lvalue";
inline constexpr const char *EvaluationOrder = "evaluation-order";
inline constexpr const char *ReceiverCaptured = "receiver-captured";
inline constexpr const char *DeclaredOutsideWorkspace = "declared-outside-workspace";
inline constexpr const char *NameCollision = "name-collision";
inline constexpr const char *UnsupportedOption = "unsupported-option";

inline constexpr const char *NotAStaticMember = "not-a-static-member";
inline constexpr const char *InvalidTarget = "invalid-target";
inline constexpr const char *UnsupportedTarget = "unsupported-target";
inline constexpr const char *DynamicInitialization = "dynamic-initialization";
inline constexpr const char *UnsupportedMember = "unsupported-member";
inline constexpr const char *DiscardedExpression = "discarded-expression";

inline constexpr const char *InternalVerificationFailed = "internal-verification-failed";

} // namespace riktig::refusal_codes
