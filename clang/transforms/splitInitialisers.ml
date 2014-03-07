open Clang

let (%) f g = fun x -> f (g x)


let transform_decl =
  let open Ast in

  let rec v = Visitor.({
    map_desg = (fun state desg -> visit_desg v state desg);
    map_decl = (fun state desg -> visit_decl v state desg);
    map_expr = (fun state desg -> visit_expr v state desg);
    map_ctyp = (fun state desg -> visit_ctyp v state desg);
    map_tloc = (fun state desg -> visit_tloc v state desg);
    map_stmt;
  })


  and map_stmt state stmt =
    match stmt.s with
    | CompoundStmt stmts ->
        let stmts =
          List.fold_left (fun stmts stmt ->
            let (state, stmt) = map_stmt state stmt in
            match state with
            | [] -> stmt :: stmts
            | xs -> xs @ stmts
          ) [] stmts
        in

        (state, { stmt with s = CompoundStmt (List.rev stmts) })

    | DeclStmt [{ d = VarDecl (ty, name, Some init) } as decl] ->
        let init_stmt = {
          s = ExprStmt {
              e = BinaryOperator (
                  BO_Assign,
                  { e = DeclRefExpr name;
                    e_sloc = decl.d_sloc;
                    e_type = Api.(request @@ TypePtr ty.tl_cref);
                    e_cref = Ref.null;
                  },
                  init
                );
              e_type = init.e_type;
              e_sloc = decl.d_sloc;
              e_cref = Ref.null;
            };
          s_sloc = init.e_sloc;
          s_cref = Ref.null;
        } in

        let state = [
          init_stmt;
          { stmt with
            s = DeclStmt [{ decl with d = VarDecl (ty, name, None) }];
          };
        ] in

        (state, stmt)

    | _ ->
        Visitor.visit_stmt v state stmt

  in

  snd % Visitor.visit_decl v []
