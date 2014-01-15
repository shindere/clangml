(* Very beginnings of a basic C++ code generation module *)
(* Clients can construct a C++ AST and this module serializes*)
(* it to source code. *)

module Log = Logger.Make(struct let tag = "codegen" end)


(*****************************************************
 * C++ data types
 *****************************************************)


type enum_element = string * int option
  deriving (Show)


type enum_intf = {
  enum_name : string;
  enum_elements : enum_element list;
} deriving (Show)


type flag =
  | Explicit
  | Virtual
  | Static
  | Const
  deriving (Show)


type cpp_type =
  (* Built-in basic types (or std:: types) *)
  | TyVoid
  | TyInt
  | TyString
  (* Typedef-name *)
  | TyName of string
  (* Parameterised types *)
  | TyPointer of cpp_type
  | TyReference of cpp_type
  | TyTemplate of string * cpp_type
  deriving (Show)


type declaration = {
  decl_flags : flag list;
  decl_type : cpp_type;
  decl_name : string;
  decl_init : string;
} deriving (Show)

let empty_decl = {
  decl_flags = [];
  decl_type = TyVoid;
  decl_name = "";
  decl_init = "";
}


type expression =
  | IdExpr of string
  | IntExpr of int
  | FCall of (* name *)string * (* arguments *)expression list
  deriving (Show)


type statement =
  | CompoundStmt of statement list
  | Return of expression
  | Expression of expression
  deriving (Show)


let empty_body = CompoundStmt []


type class_member =
  | MemberField
    of (* field-decl *)declaration
  | MemberFunction
    of (* flags *)flag list
     * (* return-type *)cpp_type
     * (* name *)string
     * (* arguments *)declaration list
     * (* this-flags *)flag list
     * (* body *)statement
  | MemberConstructor
    of (* flags *)flag list
     * (* arguments *)declaration list
     * (* init-list *)(string * string) list
     * (* body *)statement
  deriving (Show)


type class_intf = {
  class_name : string;
  class_bases : string list;
  class_fields : class_member list;
  class_methods : class_member list;
} deriving (Show)


type intf =
  | Enum of enum_intf
  | Class of class_intf
  | Forward of string
  deriving (Show)



(*****************************************************
 * Stringification of some of the above.
 *****************************************************)


let string_of_flag = function
  | Explicit -> "explicit"
  | Virtual -> "virtual"
  | Static -> "static"
  | Const -> "const"

let rec string_of_cpp_type = function
  | TyInt -> "int"
  | TyName name -> name
  | TyPointer ty -> string_of_cpp_type ty ^ "*"
  | TyTemplate (template, ty) -> template ^ "<" ^ string_of_cpp_type ty ^ ">"
  | ty ->
      Log.unimp "type: %a"
        Show.format<cpp_type> ty


(*****************************************************
 * Code generation
 *****************************************************)


type codegen_state = {
  output : Format.formatter;
}

type t = codegen_state


let make_codegen_with_channel (oc : out_channel) : t =
  let formatter = Formatx.formatter_of_out_channel oc in
  { output = formatter }


let make_codegen (path : string) : t =
  let oc = open_out path in
  make_codegen_with_channel oc


let flush cg : unit =
  Formatx.pp_print_flush cg.output ()


(*****************************************************
 * Interface
 *****************************************************)


let emit_enum_element fmt (name, value) =
  match value with
  | None -> Formatx.fprintf fmt "%s" name
  | Some i -> Formatx.fprintf fmt "%s = %d" name i


let emit_enum_intf fmt (i : enum_intf) =
  let pp_enum_list =
    Formatx.pp_list ~sep:Formatx.pp_comma_sep emit_enum_element
  in
  Formatx.fprintf fmt
    "@[<v>enum %s@,\
     {@,\
     @[<v2>  %a@]@,\
     };@]"
    i.enum_name
    pp_enum_list i.enum_elements


let emit_type fmt ty =
  Format.pp_print_string fmt (string_of_cpp_type ty ^ " ")


let emit_declaration fmt decl =
  Formatx.fprintf fmt "%a%s"
    emit_type decl.decl_type
    decl.decl_name


let pp_argument_list =
  Formatx.pp_list ~sep:Formatx.pp_comma_sep emit_declaration

let pp_flag_list =
  let emit_flag fmt flag =
    Format.pp_print_string fmt (string_of_flag flag ^ " ")
  in
  Formatx.pp_list ~sep:(Formatx.pp_sep "") emit_flag

let pp_this_flag_list =
  let emit_flag fmt flag =
    Format.pp_print_string fmt (" " ^ string_of_flag flag)
  in
  Formatx.pp_list ~sep:(Formatx.pp_sep "") emit_flag

let pp_initialiser fmt = function
  | "" -> ()
  | init ->
      Format.pp_print_string fmt " = ";
      Format.pp_print_string fmt init


let emit_class_member_intf class_name fmt = function
  | MemberField { decl_flags; decl_type; decl_name; decl_init; } ->
      Formatx.fprintf fmt "%a%a%s%a;"
        pp_flag_list decl_flags
        emit_type decl_type
        decl_name
        pp_initialiser decl_init
  | MemberFunction (flags, retty, name, args, this_flags, body) ->
      Formatx.fprintf fmt "%a%a%s (%a)%a;"
        pp_flag_list flags
        emit_type retty
        name
        pp_argument_list args
        pp_this_flag_list this_flags
  | MemberConstructor (flags, args, init, body) ->
      Formatx.fprintf fmt "%a%s (%a);"
        pp_flag_list flags
        class_name
        pp_argument_list args


let emit_base_classes fmt bases =
  let pp_base_list =
    Formatx.pp_list ~sep:Formatx.pp_comma_sep Format.pp_print_string
  in
  match bases with
  | [] -> ()
  | xs ->
      Formatx.fprintf fmt
        " : %a"
        pp_base_list xs
  


let emit_class_intf fmt (i : class_intf) : unit =
  (*Log.unimp "emit_class_intf: %s"*)
    (*(Show.show<class_intf> i)*)
  let pp_member_list =
    Formatx.pp_list ~sep:(Formatx.pp_sep "") (emit_class_member_intf i.class_name)
  in
  Formatx.fprintf fmt
    "@[<v>struct %s%a@,\
     {@,\
     @[<v2>  %a@]@,\
     };@]"
    i.class_name
    emit_base_classes i.class_bases
    pp_member_list (i.class_fields @ i.class_methods)


let emit_intf fmt = function
  | Enum i -> emit_enum_intf fmt i
  | Class i -> emit_class_intf fmt i
  | Forward name ->
      Formatx.fprintf fmt "struct %s;" name


let emit_intfs basename cg cpp_types =
  let pp_intf_list =
    Formatx.pp_list ~sep:(Formatx.pp_sep "\n") emit_intf
  in
  let ucasename = String.uppercase basename in
  Formatx.fprintf cg.output
    "@[<v0>#ifndef %s_H@,\
     #define %s_H@,\
     #include \"ocaml++.h\"@,\
     @,\
     %a@,\
     @,\
     #endif /* %s_H */@]@."
    ucasename ucasename
    pp_intf_list cpp_types
    ucasename


(*****************************************************
 * Implementation
 *****************************************************)


let emit_function_decl fmt (class_name, func_name, args, this_flags) =
  Formatx.fprintf fmt "%s::%s (%a)%a@\n"
    class_name
    func_name
    pp_argument_list args
    pp_this_flag_list this_flags


let emit_ctor_init fmt (member, initialiser) =
  Formatx.fprintf fmt "%s (%s)"
    member initialiser


let emit_ctor_init_list fmt = function
  | [] -> ()
  | init ->
      let pp_init_list =
        Formatx.pp_list ~sep:Formatx.pp_comma_sep emit_ctor_init
      in
      Formatx.fprintf fmt "  : %a@\n"
        pp_init_list init


let rec emit_expression fmt = function
  | IdExpr id ->
      Formatx.pp_print_string fmt id
  | IntExpr value ->
      Formatx.pp_print_int fmt value
  | FCall (name, args) ->
      Formatx.fprintf fmt "%s (%a)"
        name
        (Formatx.pp_list ~sep:Formatx.pp_comma_sep
          emit_expression) args


let rec emit_statement fmt = function
  | CompoundStmt stmts ->
      Formatx.fprintf fmt "@[<v2>{@,%a@]@\n}"
        (Formatx.pp_list ~sep:(Formatx.pp_sep "\n")
          emit_statement) stmts
  | Return expr ->
      Formatx.fprintf fmt "return %a;"
        emit_expression expr
  | Expression expr ->
      Formatx.fprintf fmt "%a;"
        emit_expression expr


let emit_class_member_impl class_name fmt = function
  | MemberFunction (flags, retty, name, args, this_flags, body) ->
      Formatx.fprintf fmt "%a%a%a"
        emit_type retty
        emit_function_decl (class_name, name, args, this_flags)
        emit_statement body
  | MemberConstructor (flags, args, init, body) ->
      Formatx.fprintf fmt "%a%a%a"
        emit_function_decl (class_name, class_name, args, [])
        emit_ctor_init_list init
        emit_statement body
  | MemberField { decl_flags; decl_type; decl_name; decl_init; } ->
      Log.bug "fields have no implementation"


let emit_class_impl fmt (i : class_intf) : unit =
  Formatx.fprintf fmt "@[<v>@,%a@]"
    (Formatx.pp_list ~sep:(Formatx.pp_sep "\n")
      (emit_class_member_impl i.class_name)) i.class_methods


let emit_impl fmt = function
  | Class i -> emit_class_impl fmt i
  (* enums and forward declarations have no implementation *)
  | Forward _ | Enum _ -> ()


let emit_impls basename cg cpp_types =
  let pp_impl_list =
    Formatx.pp_list ~sep:(Formatx.pp_sep "") emit_impl
  in
  Formatx.fprintf cg.output
    "#include \"%s.h\"@,\
     @[<v>%a@]@."
    basename
    pp_impl_list cpp_types
