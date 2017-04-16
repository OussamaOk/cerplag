#include <stdio.h>
#include <stdlib.h>
#include <stdbool.h>
#include "parser.h"
#include "error.h"
#include "ast.h"
#include "ast_gen.h"
#include "symbol_table.h"
#include "codegen.h"
#include "util/vptr_int_hmap.h"

#define MODULE_DECLARED 1
#define MODULE_DEFINED  2
#define MODULE_USED     4

typedef ModuleNode* pMN;

static void pMN_destroy(pMN p){
}
static void pMN_print(pMN p, FILE* fp)
{fprintf(fp, "ModuleNode(%s, %d:%d)", p->name, p->base.line, p->base.col);}

#define KTYPE vptr
#define VTYPE pMN
#define KTYPED(x) vptr_##x
#define VTYPED(x) pMN_##x
#define ITYPED(x) vptr_pMN_##x
#include "util/hmap.gen.h"
#include "util/hmap.gen.c"
#undef KTYPE
#undef VTYPE
#undef KTYPED
#undef VTYPED
#undef ITYPED

vptr_int_hmap module_status;
vptr_pMN_hmap module_node_map;
SD mySD;

static char msg[200];

void print_type_error(op_t op, valtype_t type1, valtype_t type2, int line, int col) {
    sprintf(msg, "Operands are of the wrong type ('%s' and '%s') for operator '%s'.",
        TYPE_STRS[type1], TYPE_STRS[type2], OP_STRS[op]);
    print_error("type", ERROR, 10, line, col, NULL, NULL, msg);
}

void print_undecl_id_error(const char* varname, int line, int col) {
    print_error("compile", ERROR, 11, line, col, varname, "UNDECL_ID", "Undeclared identifier");
}

valtype_t get_composite_type(op_t op, valtype_t type1, valtype_t type2, int line, int col) {
    if(type1 == TYPE_ERROR || type2 == TYPE_ERROR)
        return TYPE_ERROR;
    if(type1 != type2) {
        print_type_error(op, type1, type2, line, col);
        return TYPE_ERROR;
    }
    switch(op) {
        case OP_PLUS:
        case OP_MINUS:
        case OP_MUL:
            if(type1 == TYPE_INTEGER || type1 == TYPE_REAL)
                return type1;
            else
                print_type_error(op, type1, type2, line, col);
            break;
        case OP_DIV:
            if(type1 == TYPE_INTEGER || type1 == TYPE_REAL)
                return TYPE_REAL;
            else
                print_type_error(op, type1, type2, line, col);
            break;
        case OP_GT:
        case OP_LT:
        case OP_GE:
        case OP_LE:
        case OP_EQ:
        case OP_NE:
            if(type1 == TYPE_INTEGER || type1 == TYPE_REAL)
                return TYPE_BOOLEAN;
            else
                print_type_error(op, type1, type2, line, col);
            break;
        case OP_AND:
        case OP_OR:
            if(type1 == TYPE_BOOLEAN)
                return TYPE_BOOLEAN;
            else
                print_type_error(op, type1, type2, line, col);
            break;
        default:
            fprintf(stderr, "Invalid operand %d\n", op);
    }
    return TYPE_ERROR;
}

void compile_node(pAstNode p);

void compile_node_chain(pAstNode p) {
    while(p != NULL) {
        compile_node(p);
        p = get_next_ast_node(p);
    }
}

static void get_type_str(char* str, valtype_t type, int size) {
    if(size == 0)
        sprintf(str, "%s", TYPE_STRS[type]);
    else
        sprintf(str, "%s[%d]", TYPE_STRS[type], size);
}

void add_idTypeListNode_to_SD(IDTypeListNode* node) {
    pSTEntry entry = malloc(sizeof(STEntry));
#ifdef LOG_MEM
    fprintf(stderr, "%s: Allocated STEntry %p\n", __func__, (void*)entry);
#endif
    entry->type = node->base.type;
    entry->size = node->base.size;
    entry->line = node->base.line;
    entry->col = node->base.col;
    entry->offset = 0;
    entry->readonly = false;
    entry->lexeme = node->varname;
    SD_add_entry(&mySD, entry);
    //free(entry);
#ifdef LOG_MEM
    fprintf(stderr, "%s: Freed STEntry %p\n", __func__, (void*)entry);
#endif
}

void compare_lists_type(const char* func_name, bool is_output, IDListNode* actual, IDTypeListNode* formal) {
    int acount=0, fcount=0;
    IDListNode* org_actual = actual;
    IDTypeListNode* org_formal = formal;
    for(; actual != NULL; actual = actual->next, acount++);
    for(; formal != NULL; formal = formal->next, fcount++);
    actual = org_actual;
    formal = org_formal;
    if(acount < fcount) {
        print_error("type", ERROR, 41, actual->base.line, actual->base.col, func_name,
            "FEW_ARGS", "Too few arguments to function.");
    }
    else if(acount > fcount) {
        print_error("type", ERROR, 42, actual->base.line, actual->base.col, func_name,
            "MANY_ARGS", "Too many arguments to function.");
    }
    else {
        const char* list_type = is_output? "output": "input";
        while(actual != NULL) {
            int ftype = formal->base.type;
            int fsize = formal->base.size;
            pSTEntry entry = SD_get_entry(&mySD, actual->varname);
            if(entry == NULL) {
                print_undecl_id_error(actual->varname, actual->base.line, actual->base.col);
            }
            else {
                if(entry->readonly == true) {
                    sprintf(msg, "Cannot modify read-only variable '%s' (declared at line %d, col %d).",
                        actual->varname, entry->line, entry->col);
                    print_error("type", ERROR, 50, actual->base.line, actual->base.col, actual->varname,
                        "RDONLY_MOD", msg);
                }
                int atype = entry->type;
                int asize = entry->size;
                if(!(atype == TYPE_ERROR || (atype == ftype && asize == fsize))) {
                    char fstr[24], astr[24];
                    get_type_str(astr, atype, asize);
                    get_type_str(fstr, ftype, fsize);
                    sprintf(msg, "Actual %s parameter has type %s, but formal %s parameter has type %s.",
                        list_type, astr, list_type, fstr);
                    print_error("type", ERROR, 43, actual->base.line, actual->base.col, actual->varname,
                        "FCALL_TYPE_MISMATCH", msg);
                }
            }
            actual = actual->next;
            formal = formal->next;
        }
    }
}

void compile_node(pAstNode p) {
    if(p == NULL) return;
    //fprintf(stderr, "%s(%s)\n", __func__, ASTN_STRS[p->base.node_type]);
    switch(p->base.node_type) {
        case ASTN_BOp: {
            BOpNode* q = (BOpNode*)p;
            compile_node(q->arg1);
            compile_node(q->arg2);
            valtype_t type1 = q->arg1->base.type, type2 = q->arg2->base.type;
            q->base.size = 0;
            int size1 = q->arg1->base.size, size2 = q->arg2->base.size;
            if(size1 == 0 && size2 == 0) {
                q->base.type = get_composite_type(q->op, type1, type2, q->base.line, q->base.col);
            }
            else {
                if(size1 > 0) {
                    print_error("type", ERROR, 12, q->arg1->base.line, q->arg1->base.col,
                        OP_STRS[q->op], NULL, "Left operand cannot be an array.");
                }
                if(size2 > 0) {
                    print_error("type", ERROR, 13, q->arg2->base.line, q->arg2->base.col,
                        OP_STRS[q->op], NULL, "Right operand cannot be an array.");
                }
                q->base.type = TYPE_ERROR;
            }
            break;
        }
        case ASTN_UOp: {
            UOpNode* q = (UOpNode*)p;
            compile_node(q->arg);
            valtype_t subtype = q->arg->base.type;
            q->base.size = 0;
            int subsize = q->arg->base.size;
            if(subsize == 0) {
                if(subtype == TYPE_INTEGER || subtype == TYPE_REAL) {
                    q->base.type = subtype;
                }
                else if(subtype == TYPE_ERROR) {
                    q->base.type = TYPE_ERROR;
                }
                else {
                    q->base.type = TYPE_ERROR;
                    sprintf(msg, "Operand's type should be INTEGER or REAL, not %s.", TYPE_STRS[subtype]);
                    print_error("type", ERROR, 14, q->arg->base.line, q->arg->base.col,
                        OP_STRS[q->op], NULL, msg);
                }
            }
            else {
                print_error("type", ERROR, 15, q->arg->base.line, q->arg->base.col,
                    OP_STRS[q->op], NULL, "Operand cannot be an array.");
                q->base.type = TYPE_ERROR;
            }
            break;
        }
        case ASTN_Deref: {
            DerefNode* q = (DerefNode*)p;
            compile_node(q->index);
            q->base.size = 0;
            pSTEntry entry = SD_get_entry(&mySD, q->varname);
            if(entry == NULL) {
                print_undecl_id_error(q->varname, q->base.line, q->base.col);
                q->base.type = TYPE_ERROR;
            }
            else if(entry->size == 0) {
                print_error("type", ERROR, 16, q->base.line, q->base.col, q->varname, "NONARR_DEREF",
                    "Dereference operator applied on a variable which is not an array.");
                q->base.type = TYPE_ERROR;
            }
            else {
                q->base.type = entry->type;
                int size = entry->size;
                if(q->index->base.node_type == ASTN_Var) {
                    if(!(q->index->base.type == TYPE_INTEGER && q->index->base.size == 0)) {
                        if(q->index->base.size > 0) {
                            sprintf(msg, "Array index should be an integer, not %s[%d].",
                                TYPE_STRS[q->index->base.type], q->index->base.size);
                        }
                        else {
                            sprintf(msg, "Array index should be an integer, not %s.",
                                TYPE_STRS[q->index->base.type]);
                        }
                        print_error("type", ERROR, 17, q->base.line, q->base.col, q->varname, "NONINT_INDEX", msg);
                    }
                }
                else {
                    int val = ((NumNode*)(q->index))->val;
                    if(val <= 0 || val > size) {
                        sprintf(msg, "Index of %s should be in the range 1 to %d.\n", q->varname, entry->size);
                        print_error("type", ERROR, 18, q->index->base.line, q->index->base.col, NULL, "OOB_INDEX", msg);
                    }
                }
            }
            break;
        }
        case ASTN_Var: {
            VarNode* q = (VarNode*)p;
            pSTEntry entry = SD_get_entry(&mySD, q->varname);
            if(entry == NULL) {
                print_undecl_id_error(q->varname, q->base.line, q->base.col);
                q->base.type = TYPE_ERROR;
                q->base.size = 0;
            }
            else {
                q->base.type = entry->type;
                q->base.size = entry->size;
            }
            break;
        }
        case ASTN_Num:
        case ASTN_RNum:
        case ASTN_Bool:
        case ASTN_Range:
            break;
        case ASTN_Module: {
            ModuleNode* q = (ModuleNode*)p;
            if(q->name != NULL) {
                vptr_int_hmap_node* hmap_node = vptr_int_hmap_insert(&module_status, q->name, 0);
                hmap_node->value |= MODULE_DEFINED;
                int status = hmap_node->value;
                if((status & MODULE_DECLARED) && (~status & MODULE_USED)) {
                    print_error("compile", WARNING, 6, q->base.line, q->base.col, q->name,
                        "NEEDLESS_DECLARE", "Module has been declared but not used before defining it.");
                }
            }
            IDTypeListNode* node = NULL;
            node = q->iParamList;
            while(node != NULL) {
                add_idTypeListNode_to_SD(node);
                node = node->next;
            }
            node = q->oParamList;
            while(node != NULL) {
                add_idTypeListNode_to_SD(node);
                node = node->next;
            }
            compile_node_chain(q->body);
            break;
        }
        case ASTN_Assn: {
            AssnNode* q = (AssnNode*)p;
            compile_node(q->target);
            compile_node(q->expr);
            valtype_t type1 = q->target->base.type, type2 = q->expr->base.type;
            int size1 = q->target->base.size, size2 = q->expr->base.size;
            if((type1 != type2 || size1 != size2) && type2 != TYPE_ERROR) {
                char tstr1[24], tstr2[24];
                get_type_str(tstr1, type1, size1);
                get_type_str(tstr2, type2, size2);
                sprintf(msg, "Type of target is %s, but type of expression is %s.", tstr1, tstr2);
                print_error("type", ERROR, 19, q->base.line, q->base.col, NULL, NULL, msg);
            }
            const char* varname = NULL;
            if(q->target->base.node_type == ASTN_Deref) {
                varname = ((DerefNode*)(q->target))->varname;
            }
            else if(q->target->base.node_type == ASTN_Var) {
                varname = ((VarNode*)(q->target))->varname;
            }
            pSTEntry entry = SD_get_entry(&mySD, varname);
            if(entry != NULL && entry->readonly) {
                sprintf(msg, "Cannot modify read-only variable '%s' (declared at line %d, col %d).",
                    varname, entry->line, entry->col);
                print_error("type", ERROR, 50, q->target->base.line, q->target->base.col, varname,
                    "RDONLY_MOD", msg);
            }
            break;
        }
        case ASTN_While: {
            WhileNode* q = (WhileNode*)p;
            compile_node(q->cond);
            int type = q->cond->base.type;
            int size = q->cond->base.size;
            if(!(q->cond->base.type == TYPE_BOOLEAN && q->cond->base.size == 0)) {
                char tstr[24];
                get_type_str(tstr, type, size);
                sprintf(msg, "While loop's condition has type %s instead of BOOLEAN.", tstr);
                print_error("type", ERROR, 20, q->cond->base.line, q->cond->base.col, NULL, "NONBOOL_COND", msg);
            }
            SD_add_scope(&mySD, p);
            compile_node_chain(q->body);
            SD_remove_scope(&mySD);
            break;
        }
        case ASTN_For: {
            ForNode* q = (ForNode*)p;
            pSTEntry entry = SD_get_entry(&mySD, q->varname);
            if(entry == NULL) {
                print_undecl_id_error(q->varname, q->base.line, q->base.col);
            }
            else if(!(entry->type == 1 && entry->size == 0)) {
                char tstr[24];
                get_type_str(tstr, entry->type, entry->size);
                sprintf(msg, "Loop variable %s should be an INTEGER, not %s.", q->varname, tstr);
                print_error("type", ERROR, 24, q->base.line, q->base.col, NULL, "LOOPVAR_NOTINT", msg);
            }
            bool prev_readonly = false;
            int prev_line = 0, prev_col = 0;
            if(entry != NULL) {
                prev_readonly = entry->readonly;
                prev_line = entry->line;
                prev_col = entry->col;
                entry->readonly = true;
                entry->line = q->base.line;
                entry->col = q->base.col;
            }
            if(prev_readonly) {
                pSTEntry entry = SD_get_entry(&mySD, q->varname);
                if(entry != NULL && entry->readonly) {
                    sprintf(msg, "Cannot use read-only variable '%s' (declared at line %d, col %d) for looping.",
                        q->varname, entry->line, entry->col);
                    print_error("type", ERROR, 51, q->base.line, q->base.col, q->varname,
                        "RDONLY_LOOPVAR", msg);
                }
            }
            SD_add_scope(&mySD, p);
            compile_node_chain(((ForNode*)p)->body);
            SD_remove_scope(&mySD);
            if(entry != NULL) {
                entry->readonly = prev_readonly;
                entry->line = prev_line;
                entry->col = prev_col;
            }
            break;
        }
        case ASTN_Decl: {
            DeclNode* q = (DeclNode*)p;
            IDListNode* node = (IDListNode*)(q->idList);
            while(node != NULL) {
                pSTEntry entry = malloc(sizeof(STEntry));
#ifdef LOG_MEM
                fprintf(stderr, "%s: Allocated STEntry %p\n", __func__, (void*)entry);
#endif
                entry->type = q->base.type;
                entry->size = q->base.size;
                entry->line = node->base.line;
                entry->col = node->base.col;
                entry->offset = 0;
                entry->readonly = false;
                entry->lexeme = node->varname;
                SD_add_entry(&mySD, entry);
                //free(entry);
#ifdef LOG_MEM
                fprintf(stderr, "%s: Freed STEntry %p\n", __func__, (void*)entry);
#endif
                node = node->next;
            }
            break;
        }
        case ASTN_Input:
            break;
        case ASTN_Output:
            compile_node(((OutputNode*)p)->var);
            break;
        case ASTN_FCall: {
            FCallNode* q = (FCallNode*)p;
            if(q->name == ((ModuleNode*)(SD_get_root(&mySD)->scope))->name) {
                print_error("compile", ERROR, 21, q->base.line, q->base.col, q->name, "RECURSE_CALL",
                    "Recursive calls are not allowed.");
            }
            vptr_pMN_hmap_node* hmap_node = vptr_pMN_hmap_query(&module_node_map, q->name);
            if(hmap_node == NULL) {
                print_undecl_id_error(q->name, q->base.line, q->base.col);
            }
            else {
                ModuleNode* module_node = hmap_node->value;
                vptr_int_hmap_node* hmap_node = vptr_int_hmap_insert(&module_status, q->name, 0);
                hmap_node->value |= MODULE_USED;
                if(!((hmap_node->value & MODULE_DEFINED) || (hmap_node->value & MODULE_DECLARED))) {
                    print_error("compile", ERROR, 22, q->base.line, q->base.col, q->name, "UNDEF_MODULE",
                        "Module must be defined or declared before using it.");
                }
                compare_lists_type(q->name, false, q->iParamList, module_node->iParamList);
                if(q->oParamList != NULL) {
                    compare_lists_type(q->name, true, q->oParamList, module_node->oParamList);
                }
            }
            break;
        }
        case ASTN_Switch: {
            SwitchNode* q = (SwitchNode*)p;
            pSTEntry entry = SD_get_entry(&mySD, q->varname);
            int type = TYPE_ERROR;
            char tstr[24] = "ERROR", tstr2[24];
            if(entry == NULL) {
                print_undecl_id_error(q->varname, q->base.line, q->base.col);
            }
            else if(entry->size > 0 || (entry->type != TYPE_INTEGER && entry->type != TYPE_BOOLEAN)) {
                get_type_str(tstr2, entry->type, entry->size);
                sprintf(msg, "Switch variable's type should be INTEGER or BOOLEAN, not %s", tstr2);
                print_error("type", ERROR, 25, q->base.line, q->base.col, q->varname, NULL, msg);
            }
            else {
                type = entry->type;
                get_type_str(tstr, type, 0);
            }
            CaseNode* node = q->cases;
            int count[2] = {0, 0};
            SD_add_scope(&mySD, p);
            while(node != NULL) {
                compile_node(node->val);
                int type2 = node->val->base.type;
                int size2 = node->val->base.size;
                if(size2 > 0 || (type2 != type && type != TYPE_ERROR)) {
                    get_type_str(tstr2, type2, size2);
                    sprintf(msg, "Switch variable has type %s, but case variable has type %s.", tstr, tstr2);
                    print_error("type", ERROR, 26, node->val->base.line, node->val->base.col, NULL, NULL, msg);
                }
                else if(type == TYPE_BOOLEAN) {
                    if(node->val->base.node_type == ASTN_Bool) {
                        bool val = ((BoolNode*)(node->val))->val;
                        count[val & 1]++;
                        if(count[val & 1] > 1) {
                            print_error("type", ERROR, 27, node->val->base.line, node->val->base.col,
                                NULL, NULL, "Duplicate value in switch statement.");
                        }
                    }
                    else {
                        fprintf(stderr, "Assert failure in boolean switch.\n");
                    }
                }
                compile_node_chain((pAstNode)(node->stmts));
                node = node->next;
            }
            if(type == TYPE_BOOLEAN) {
                if(count[0] == 0) {
                    print_error("type", ERROR, 28, p->base.line, p->base.col,
                        NULL, NULL, "The case 'false' hasn't been handled.");
                }
                if(count[1] == 0) {
                    print_error("type", ERROR, 28, p->base.line, p->base.col,
                        NULL, NULL, "The case 'true' hasn't been handled.");
                }
            }
            if(q->defaultcase != NULL) {
                if(type == TYPE_BOOLEAN) {
                    print_error("type", ERROR, 27, q->defaultcase->base.line, q->defaultcase->base.col,
                        NULL, NULL, "Default case is not allowed when switch variable is BOOLEAN.");
                }
                compile_node_chain((pAstNode)(q->defaultcase->stmts));
            }
            else if(type == TYPE_INTEGER) {
                print_error("type", ERROR, 28, q->base.line, q->base.col, NULL, NULL,
                    "Default case is mandatory when switch variable is an INTEGER.");
            }
            SD_remove_scope(&mySD);
            break;
        }
        default:
            complain_ast_node_type(__func__, p->base.node_type);
    }
	gen_intermediate_code(&mySD,p);
}

void compile(ProgramNode* root) {
    vptr_int_hmap_init(&module_status, 10, false);
    vptr_pMN_hmap_init(&module_node_map, 10, false);
    SD_init(&mySD);

    IDListNode* decl_node = NULL;
    ModuleNode* module_node = NULL;

    decl_node = root->decls;
    while(decl_node != NULL) {
        int old_size = module_status.size;
        vptr_int_hmap_insert(&module_status, decl_node->varname, MODULE_DECLARED);
        if(module_status.size == old_size) {
            sprintf(msg, "Module '%s' has already been declared.", decl_node->varname);
            print_error("compile", ERROR, 1, decl_node->base.line, decl_node->base.col, NULL, "MODULE_REDECL", msg);
        }
        decl_node = decl_node->next;
    }

    module_node = root->modules;
    while(module_node != NULL) {
        if(module_node->name != NULL) {
            vptr_int_hmap_insert(&module_status, module_node->name, 0);
            int old_size = module_node_map.size;
            ModuleNode* n2 = vptr_pMN_hmap_insert(&module_node_map, module_node->name, module_node)->value;
            if(module_node_map.size == old_size) {
                sprintf(msg, "Module '%s' has already been defined on line %d col %d.",
                    module_node->name, n2->base.line, n2->base.col);
                print_error("compile", ERROR, 2, module_node->base.line, module_node->base.col, NULL, "MODULE_REDEF", msg);
            }
        }
        module_node = module_node->next;
    }

    module_node = root->modules;
    while(module_node != NULL) {
        SD_add_scope(&mySD, (pAstNode)module_node);
        compile_node((pAstNode)module_node);
        SD_remove_scope(&mySD);
        module_node = module_node->next;
    }

    //vptr_pMN_hmap_print(&module_node_map, stderr);
    //vptr_int_hmap_print(&module_status, stderr);

    decl_node = root->decls;
    while(decl_node != NULL) {
        int status = vptr_int_hmap_get(&module_status, decl_node->varname);
        if((status & MODULE_DECLARED) && (~status & MODULE_DEFINED)) {
            // module is declared but not defined
            sprintf(msg, "Module '%s' has been declared but not defined.", decl_node->varname);
            print_error("compile", ERROR, 3, decl_node->base.line, decl_node->base.col, NULL, "MODULE_NOTDECL", msg);
        }
        decl_node = decl_node->next;
    }

    module_node = root->modules;
    while(module_node != NULL) {
        if(module_node->name != NULL) {
            int status = vptr_int_hmap_get(&module_status, module_node->name);
            if(~status & MODULE_USED) {
                sprintf(msg, "Module '%s' has not been used.", module_node->name);
                print_error("compile", WARNING, 4, module_node->base.line, module_node->base.col, NULL, "MODULE_NOTUSED", msg);
            }
        }
        module_node = module_node->next;
    }

    vptr_int_hmap_clear(&module_status);
    vptr_pMN_hmap_clear(&module_node_map);
    SD_clear(&mySD);
}

int compiler_main(FILE* ifp, FILE* ofp, int verbosity) {
    gsymb_t start_symb = init_parser();

    parse_tree_node* proot = build_parse_tree(ifp, start_symb);
    int parse_errors = error_count;
    ProgramNode* ast = NULL;

    if(parse_errors == 0) {
        build_ast(proot);
        ast = (ProgramNode*)proot->value->tree;
        if(ast->base.node_type != ASTN_Program) {
            fprintf(stderr, "Root node of AST is %s instead of ProgramNode", ASTN_STRS[ast->base.node_type]);
            abort();
        }
    }
    parse_tree_destroy(proot);
    destroy_parser(false);

    if(parse_errors == 0) {
        compile(ast);
        destroy_ast((pAstNode)ast);
    }

    pch_int_hmap_clear(&intern_table);
    return 0;
}
