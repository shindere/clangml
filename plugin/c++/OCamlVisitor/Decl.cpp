#include "OCamlVisitor.h"

const char* getNameAsString (clang::DeclarationName const &name)
{
  // name.getAsString().c_str() make linking with gcc5-compiled
  // libraries fail; for example on Ubuntu 15.10
  // assert( strcmp(name.getAsString().c_str(),
  //                name.getAsIdentifierInfo()->getNameStart()) == 0);
  return name.getAsIdentifierInfo()->getNameStart();
}

ptr<DeclarationName>
OCamlVisitor::translate_declaration_name (clang::DeclarationName const &name)
{
  switch (name.getNameKind ())
    {
    case clang::DeclarationName::Identifier:
      // FBR: add assert stmt
      return mkDN_Identifier( strdup (getNameAsString (name) ));
    case clang::DeclarationName::ObjCZeroArgSelector:
      printf ("ObjCZeroArgSelector: %s\n", getNameAsString (name));
      break;
    case clang::DeclarationName::ObjCOneArgSelector:
      printf ("ObjCOneArgSelector: %s\n", getNameAsString (name));
      break;
    case clang::DeclarationName::ObjCMultiArgSelector:
      printf ("ObjCMultiArgSelector: %s\n", getNameAsString (name));
      break;
    case clang::DeclarationName::CXXConstructorName:
      return mkDN_CXXConstructorName (must_traverse (name.getCXXNameType ()));
    case clang::DeclarationName::CXXDestructorName:
      return mkDN_CXXDestructorName (must_traverse (name.getCXXNameType ()));
    case clang::DeclarationName::CXXConversionFunctionName:
      return mkDN_CXXConversionFunctionName ();
    case clang::DeclarationName::CXXOperatorName:
      return mkDN_CXXOperatorName
        (translate_overloaded_operator_kind (name.getCXXOverloadedOperator ()));
    case clang::DeclarationName::CXXLiteralOperatorName:
      printf ("CXXLiteralOperatorName: %s\n", getNameAsString (name));
      break;
    case clang::DeclarationName::CXXUsingDirective:
      printf ("CXXUsingDirective: %s\n", getNameAsString (name));
      break;
    }

  return mkDN_Identifier ("<invalid>");
}


ptr<FieldDecl>
OCamlVisitor::createFieldDecl (clang::FieldDecl *D)
{
  ptr<FieldDecl> field = mkFieldDecl ();
  
  field->fd_type = getTypeLoc (D);
  field->fd_name = D->getName ();
  if (D->isBitField ())
    field->fd_bitw = must_traverse (D->getBitWidth ());
  if (D->hasInClassInitializer ())
    field->fd_init = must_traverse (D->getInClassInitializer ());
  field->fd_index = D->getFieldIndex ();
  field->fd_mutable = D->isMutable ();
  
  return field;
}



/****************************************************
 * {{{1 Declarations
 */

bool
OCamlVisitor::TraverseDecl (clang::Decl *D)
{
  Base::TraverseDecl (D);

  ptr<Decl> decl = mkDecl ();
  decl->d        = stack.pop ();
  decl->d_cref   = ref (D);
  decl->d_sloc   = sloc (D);

  return stack.push (decl);
}


bool
OCamlVisitor::TraverseFunctionDecl (clang::FunctionDecl *D)
{
  TRACE;

  // TODO: what are these? probably irrelevant in C.
  //TraverseNestedNameSpecifierLoc (D->getQualifierLoc ());
  //TraverseDeclarationNameInfo (D->getNameInfo ());

  // Function type, including parameters.
  ptr<Tloc> type;
  if (D->getTypeSourceInfo ())
    type = getTypeLoc (D);
  else
    {
      // TODO: implement this

      // Built-in implicit function declarations such as
      // printf don't have a valid TSI.
      //ptr<Tloc> result = mkBuiltinTypeLoc (sloc (D), BT_Void);
      //list<Decl> args;// = traverse_list (param_range (D));

      // TODO: exceptions

      //type = mkFunctionProtoTypeLoc (sloc (D), result, args);

      throw std::runtime_error ("unsupported built-in function declaration");
    }

  // Function body, or None.
  option<Stmt> body;
  if (D->isThisDeclarationADefinition ())
    body = must_traverse (D->getBody ());

  // TODO: Constructor initialisers.

  // Function name.
  ptr<DeclarationName> name = translate_declaration_name (D->getDeclName ());

  return stack.push (mkFunctionDecl (type, name, body));
}


bool
OCamlVisitor::TraverseCXXMethodDecl (clang::CXXMethodDecl *D)
{
  TRACE;

  return TraverseFunctionDecl (D);
}


bool
OCamlVisitor::TraverseCXXConstructorDecl (clang::CXXConstructorDecl *D)
{
  TRACE;

  return TraverseCXXMethodDecl (D);
}


bool
OCamlVisitor::TraverseCXXDestructorDecl (clang::CXXDestructorDecl *D)
{
  TRACE;

  return TraverseCXXMethodDecl (D);
}


bool
OCamlVisitor::TraverseCXXConversionDecl (clang::CXXConversionDecl *D)
{
  TRACE;

  return TraverseCXXMethodDecl (D);
}


bool
OCamlVisitor::TraverseEmptyDecl (clang::EmptyDecl *D)
{
  TRACE;

  return stack.push (mkEmptyDecl ());
}


bool
OCamlVisitor::TraverseTypedefDecl (clang::TypedefDecl *D)
{
  TRACE;

  ptr<Tloc> type = getTypeLoc (D);
  clang::StringRef name = D->getName ();

  return stack.push (mkTypedefDecl (type, name));
}


bool
OCamlVisitor::TraverseRecordDecl (clang::RecordDecl *D)
{
  TRACE;

  TagTypeKind kind = translate_tag_type_kind (D->getTagKind ());
  option<list<Decl>> members;
  if (D->isCompleteDefinition ())
    // Some members may be implicit (from inline unions).
    members = implicit_cast<list<Decl>> (traverse_explicit_decls (D));
  clang::StringRef name = D->getName ();
  list<CxxBaseSpecifier> bases;

  return stack.push (mkRecordDecl (kind, name, members, bases));
}


bool
OCamlVisitor::TraverseCXXBaseSpecifier (clang::CXXBaseSpecifier const &B)
{
  TRACE;

  ptr<CxxBaseSpecifier> base = mkCxxBaseSpecifier ();
  base->cbs_virtual          = B.isVirtual ();
  base->cbs_base_of_class    = B.isBaseOfClass ();
  base->cbs_pack_expansion   = B.isPackExpansion ();
  base->cbs_inherit_ctors    = B.getInheritConstructors ();
  base->cbs_access_spec      =
    translate_access_specifier (B.getAccessSpecifier ());
  base->cbs_type             = must_traverse (B.getTypeSourceInfo ());

  return stack.push (base);
}


bool
OCamlVisitor::TraverseCXXRecordDecl (clang::CXXRecordDecl *D)
{
  TRACE;

  TagTypeKind kind = translate_tag_type_kind (D->getTagKind ());
  // Some members may be implicit (from inline unions).
  list<Decl> members = traverse_explicit_decls (D);
  clang::StringRef name = D->getName ();
  list<CxxBaseSpecifier> bases;
  if (D->isCompleteDefinition ())
    bases = traverse_list (base_spec_range (D));

  return stack.push (mkRecordDecl (kind, name, members, bases));
}


bool
OCamlVisitor::TraverseFieldDecl (clang::FieldDecl *D)
{
  TRACE;

  ptr<FieldDecl> field = createFieldDecl (D);

  return stack.push (mkFieldDecl (field));
}


bool
OCamlVisitor::TraverseEnumDecl (clang::EnumDecl *D)
{
  TRACE;

  clang::StringRef name = D->getName ();
  list<Decl> enumerators = traverse_list (decl_range (D));

  return stack.push (mkEnumDecl (name, enumerators));
}


bool
OCamlVisitor::TraverseEnumConstantDecl (clang::EnumConstantDecl *D)
{
  TRACE;

  clang::StringRef name = D->getName ();
  option<Expr> init = maybe_traverse (D->getInitExpr ());

  return stack.push (mkEnumConstantDecl (name, init));
}


bool
OCamlVisitor::TraverseParmVarDecl (clang::ParmVarDecl *D)
{
  TRACE;

  TraverseNestedNameSpecifierLoc (D->getQualifierLoc ());

  ptr<Tloc> type = getTypeLoc (D);
  clang::StringRef name = D->getName ();

  return stack.push (mkParmVarDecl (type, name));
}


bool
OCamlVisitor::TraverseVarDecl (clang::VarDecl *D)
{
  TRACE;

  TraverseNestedNameSpecifierLoc (D->getQualifierLoc ());

  ptr<Tloc> type = getTypeLoc (D);
  clang::StringRef name = D->getName ();
  option<Expr> init = maybe_traverse (D->getInit ());

  return stack.push (mkVarDecl (type, name, init));
}


bool
OCamlVisitor::TraverseTranslationUnitDecl (clang::TranslationUnitDecl *D)
{
  TRACE;

  // We filter out implicit declarations before iterating.
  list<Decl> decls = traverse_explicit_decls (D);

  return stack.push (mkTranslationUnitDecl (decls));
}

bool
OCamlVisitor::TraverseCapturedDecl (clang::CapturedDecl *D)
{
  TRACE;

  option<Stmt> body = maybe_traverse (D->getBody ());
  // FBR: should we bother about the implicit param decls?
  // list<Decl> params;
  // for (unsigned int i = 0; i < D->getNumParams (); ++i)
  //   params.push_back (must_traverse (D->getParam (i)));

  return stack.push (mkCapturedDecl (body));
}

#define UNIMP_DECL(CLASS)                               \
  bool OCamlVisitor::Traverse##CLASS (clang::CLASS *D)  \
  {                                                     \
    TODO;                                               \
    TRACE;                                              \
    IGNORE_ADT (CLASS, D);                              \
                                                        \
    return stack.push (mk##CLASS ());                   \
  }                                                     \


bool
OCamlVisitor::TraverseAccessSpecDecl (clang::AccessSpecDecl *D)
{
  TRACE;

  AccessSpecifier spec = translate_access_specifier (D->getAccess ());

  return stack.push (mkAccessSpecDecl (spec));
}

UNIMP_DECL (BuiltinTemplateDecl)
UNIMP_DECL (ObjCTypeParamDecl)
UNIMP_DECL (ExternCContextDecl)
UNIMP_DECL (BlockDecl)
UNIMP_DECL (ClassScopeFunctionSpecializationDecl)


bool
OCamlVisitor::TraverseClassTemplateDecl (clang::ClassTemplateDecl *D)
{
  TRACE;

  ptr<Decl> templated = must_traverse (D->getTemplatedDecl ());
  list<Decl> params = traverse_list (D->getTemplateParameters ());

  return stack.push (mkClassTemplateDecl (templated, params));
}


bool
OCamlVisitor::TraverseFileScopeAsmDecl (clang::FileScopeAsmDecl *D)
{
  TRACE;

  ptr<Expr> asmString = must_traverse (D->getAsmString ());

  return stack.push (mkFileScopeAsmDecl (asmString));
}


bool
OCamlVisitor::TraverseLabelDecl (clang::LabelDecl *D)
{
  TRACE;

  clang::StringRef name = D->getName ();

  return stack.push (mkLabelDecl (name));
}


UNIMP_DECL (FriendDecl)
UNIMP_DECL (FriendTemplateDecl)
UNIMP_DECL (FunctionTemplateDecl)
UNIMP_DECL (ImportDecl)
UNIMP_DECL (IndirectFieldDecl)


bool
OCamlVisitor::TraverseLinkageSpecDecl (clang::LinkageSpecDecl *D)
{
  TRACE;

  // We filter out implicit declarations before iterating.
  list<Decl> decls = traverse_explicit_decls (D);
  Language lang = Lang_C;
  switch (D->getLanguage ())
    {
    case clang::LinkageSpecDecl::lang_c:
      lang = Lang_C;
      break;
    case clang::LinkageSpecDecl::lang_cxx:
      lang = Lang_CXX;
      break;
    }

  return stack.push (mkLinkageSpecDecl (decls, lang));
}
UNIMP_DECL (MSPropertyDecl)
UNIMP_DECL (NamespaceAliasDecl)


bool
OCamlVisitor::TraverseNamespaceDecl (clang::NamespaceDecl *D)
{
  TRACE;

  // We filter out implicit declarations before iterating.
  list<Decl> decls = traverse_explicit_decls (D);
  clang::StringRef name = D->getName ();
  bool isInline = D->isInline ();

  return stack.push (mkNamespaceDecl (name, isInline, decls));
}

bool
OCamlVisitor::TraverseStaticAssertDecl (clang::StaticAssertDecl *D)
{
  TRACE;

  ptr<Expr> assertion = must_traverse (D->getAssertExpr ());
  clang::StringRef msg = D->getMessage ()->getString();

  return stack.push (mkStaticAssertDecl (assertion, msg));
}

bool
OCamlVisitor::TraverseObjCIvarDecl (clang::ObjCIvarDecl *D)
{
  TRACE;

  AccessControl ac = translate_access_control (D->getAccessControl ());
  ptr<FieldDecl> field_decl = createFieldDecl (D);

  return stack.push (mkObjCIvarDecl (ac, field_decl));
}

bool
OCamlVisitor::TraverseObjCInterfaceDecl (clang::ObjCInterfaceDecl *D)
{
  TRACE;

  clang::StringRef name = D->getName ();

  std::vector<clang::StringRef> referenced_protocols;
  for (clang::ObjCProtocolDecl::protocol_iterator it = D->protocol_begin ();
       it != D->protocol_end ();
       ++it)
    {
      clang::StringRef protocol_name = (*it)->getName ();
      referenced_protocols.push_back (protocol_name);
    }

  list<Decl> ivars;
  for (clang::ObjCIvarDecl* it = D->all_declared_ivar_begin ();
       it;
       it = it->getNextIvar ())
    {
      ivars.push_back (must_traverse (it));
    }

  list<Decl> methods;
  for (clang::ObjCContainerDecl::method_iterator it = D->meth_begin();
       it != D->meth_end();
       ++it)
    {
      methods.push_back (must_traverse (*it));
    }

  return stack.push
    (mkObjCInterfaceDecl (name, referenced_protocols, ivars, methods));
}


bool
OCamlVisitor::TraverseObjCCategoryDecl (clang::ObjCCategoryDecl *D)
{
  TRACE;

  clang::StringRef class_interface_name = D->getClassInterface() ->getName ();

  clang::StringRef name = D->getName ();

  std::vector<clang::StringRef> referenced_protocols;
  for (clang::ObjCProtocolDecl::protocol_iterator it = D->protocol_begin ();
       it != D->protocol_end ();
       ++it)
    {
      clang::StringRef protocol_name = (*it)->getName ();
      referenced_protocols.push_back (protocol_name);
    }

  list<Decl> methods;
  for (clang::ObjCContainerDecl::method_iterator it = D->meth_begin();
       it != D->meth_end();
       ++it)
    {
      methods.push_back (must_traverse (*it));
    }

  return stack.push
    (mkObjCCategoryDecl
     (class_interface_name, name, referenced_protocols, methods));
}


bool
OCamlVisitor::TraverseObjCProtocolDecl (clang::ObjCProtocolDecl *D)
{
  TRACE;

  clang::StringRef name = D->getName ();

  std::vector<clang::StringRef> referenced_protocols;
  for (clang::ObjCProtocolDecl::protocol_iterator it = D->protocol_begin ();
       it != D->protocol_end ();
       ++it)
    {
      clang::StringRef protocol_name = (*it)->getName ();
      referenced_protocols.push_back (protocol_name);
    }

  list<Decl> methods;
  for (clang::ObjCContainerDecl::method_iterator it = D->meth_begin();
       it != D->meth_end();
       ++it)
    {
      methods.push_back (must_traverse (*it));
    }

  return stack.push (mkObjCProtocolDecl (name, referenced_protocols, methods));
}

bool
OCamlVisitor::TraverseObjCMethodDecl (clang::ObjCMethodDecl *D)
{
  TRACE;

  ptr<Ctyp> result_type = must_traverse (D->getReturnType ());

  clang::StringRef name = D->getSelector().getNameForSlot(0);

  list<Decl> params;
  for (clang::ObjCMethodDecl::param_iterator it = D->param_begin();
       it != D->param_end();
       ++it)
    {
      params.push_back (must_traverse (*it));
    }

  return stack.push (mkObjCMethodDecl (result_type, name, params));
}


bool
OCamlVisitor::TraverseObjCImplementationDecl (clang::ObjCImplementationDecl *D)
{
  TRACE;

  clang::StringRef name = D->getName();

  list<Decl> ivars;
  for (clang::ObjCImplementationDecl::ivar_iterator it = D->ivar_begin ();
       it != D->ivar_end ();
       ++it)
    {
      ivars.push_back (must_traverse (*it));
    }

  list<Expr> initializers;
  for (clang::ObjCImplementationDecl::init_iterator it = D->init_begin ();
       it != D->init_end ();
       ++it)
    {
      initializers.push_back (must_traverse ((*it)->getInit ()));
    }

  return stack.push (mkObjCImplementationDecl (name, ivars, initializers));
}


bool
OCamlVisitor::TraverseObjCCategoryImplDecl
(clang::ObjCCategoryImplDecl *D)
{
  TRACE;

  clang::StringRef name = D->getName ();

  return stack.push (mkObjCCategoryImplDecl (name));
}


UNIMP_DECL (NonTypeTemplateParmDecl)
UNIMP_DECL (ObjCCompatibleAliasDecl)
UNIMP_DECL (ObjCPropertyDecl)
UNIMP_DECL (ObjCPropertyImplDecl)
UNIMP_DECL (OMPThreadPrivateDecl)
UNIMP_DECL (TemplateTemplateParmDecl)


bool
OCamlVisitor::TraverseTemplateTypeParmDecl (clang::TemplateTypeParmDecl *D)
{
  TRACE;

  // We filter out implicit declarations before iterating.
  ptr<Ctyp> type = must_traverse (clang::QualType (D->getTypeForDecl (), 0));
  option<Tloc> defaultArg = maybe_traverse (D->getDefaultArgumentInfo ());

  return stack.push (mkTemplateTypeParmDecl (type, defaultArg));
}
UNIMP_DECL (TypeAliasDecl)
UNIMP_DECL (TypeAliasTemplateDecl)
UNIMP_DECL (UnresolvedUsingTypenameDecl)
UNIMP_DECL (UnresolvedUsingValueDecl)


bool
OCamlVisitor::TraverseUsingDecl (clang::UsingDecl *D)
{
  TRACE;

  ptr<DeclarationName> dname = translate_declaration_name (D->getDeclName ());

  return stack.push (mkUsingDecl (dname));
}
UNIMP_DECL (UsingDirectiveDecl)
UNIMP_DECL (UsingShadowDecl)
UNIMP_DECL (VarTemplateDecl)
UNIMP_DECL (ClassTemplateSpecializationDecl)
UNIMP_DECL (ClassTemplatePartialSpecializationDecl)
UNIMP_DECL (ObjCAtDefsFieldDecl)
UNIMP_DECL (ImplicitParamDecl)
UNIMP_DECL (VarTemplateSpecializationDecl)
UNIMP_DECL (VarTemplatePartialSpecializationDecl)
// }}}
