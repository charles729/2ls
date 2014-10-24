/*******************************************************************\

Module: Summarizer

Author: Daniel Kroening, kroening@kroening.com

\*******************************************************************/

#include <iostream>

#include <util/options.h>
#include <util/i2string.h>
#include <util/simplify_expr.h>
#include <langapi/language_util.h>

#include <solvers/sat/satcheck.h>
#include <solvers/flattening/bv_pointers.h>
#include <solvers/prop/literal_expr.h>

#include "../ssa/local_ssa.h"
#include "../ssa/simplify_ssa.h"
#include "../ssa/ssa_build_goto_trace.h"
#include "../domains/ssa_analyzer.h"
#include "../ssa/ssa_unwinder.h"

#include "summarizer_fw.h"
#include "summarizer_fw_term.h"
#include "summarizer_bw.h"
//#include "summarizer_bw_term.h"

#include <cstdlib>

#include "show.h"

#include "summary_checker.h"

/*******************************************************************\

Function: summary_checkert::operator()

  Inputs:

 Outputs:

 Purpose:

\*******************************************************************/

property_checkert::resultt summary_checkert::operator()(
  const goto_modelt &goto_model)
{
  const namespacet ns(goto_model.symbol_table);
  ssa_inliner.set_message_handler(get_message_handler());

  SSA_functions(goto_model,ns);

  if(options.get_bool_option("k-induction"))
  {
    property_checkert::resultt result = property_checkert::UNKNOWN;
    unsigned max_unwind = options.get_unsigned_int_option("unwind");

    //TODO (later): loop
    //TODO (later):   refine domain
    for(unsigned unwind = 0; unwind<=max_unwind; unwind++)
    {
      status() << "Unwinding (k=" << unwind << ")" << messaget::eom;
	std::cout << "Current unwinding is " << unwind << std::endl;
      if(unwind>0) 
      {
        summary_db.clear();
        ssa_unwinder.unwind_all(unwind+1);
      }

      if(!options.get_bool_option("havoc")) 
        summarize(goto_model);

      result =  check_properties(); 
      report_statistics();
      if(result == property_checkert::PASS) 
      {
        status() << "k-induction proof found after " << unwind << " unwinding(s)" << messaget::eom;
        break;
      }
      else if(result == property_checkert::FAIL) 
      {
        status() << "k-induction counterexample found after " << unwind << " unwinding(s)" << messaget::eom;
        break;
      }
      else if(unwind==0 && max_unwind>0) //TODO: unwind==2 => 1 (additional) unwinding
      {
        ssa_unwinder.init_localunwinders();
      }
    }
    return result;
  }
  //TODO (later): done

  if(options.get_bool_option("incremental-bmc"))
  {
    property_checkert::resultt result = property_checkert::UNKNOWN;
    unsigned max_unwind = options.get_unsigned_int_option("unwind");

    for(unsigned unwind = 0; unwind<=max_unwind; unwind++)
    {
      status() << "Unwinding (k=" << unwind << ")" << eom;
	std::cout << "Current unwinding is " << unwind << std::endl;
      if(unwind>0) 
      {
        summary_db.clear();
        ssa_unwinder.unwind_all(unwind+1);
      }
      result =  check_properties(); 
      report_statistics();
      if(result == property_checkert::PASS) 
      {
        status() << "incremental BMC proof found after " << unwind 
		 << " unwinding(s)" << eom;
        break;
      }
      else if(result == property_checkert::FAIL) 
      {
        status() << "incremental BMC counterexample found after " 
		 << unwind << " unwinding(s)" << eom;
        break;
      }
      else if(unwind==0 && max_unwind>0) //TODO: unwind==2 => 1 (additional) unwinding
      {
        ssa_unwinder.init_localunwinders();
      }
    }
    return result;
  }

  // neither k-induction nor bmc
  if(!options.get_bool_option("havoc") &&
     !(!options.get_bool_option("preconditions") &&
       options.get_bool_option("termination"))) 
  {
    //forward analysis
    summarize(goto_model,true,false);
  }
  if(!options.get_bool_option("preconditions") &&
     options.get_bool_option("termination"))
  {
    //termination analysis
    summarize(goto_model,true,true);
  }
  if(!options.get_bool_option("havoc") &&
     options.get_bool_option("preconditions") &&
     !options.get_bool_option("termination"))
  {
    //backward analysis
    summarize(goto_model,false,false);
  }
  if(!options.get_bool_option("havoc") &&
     options.get_bool_option("preconditions") &&
     options.get_bool_option("termination"))
  {
    //backward analysis with termination
    summarize(goto_model,false,true);
  }

  if(options.get_bool_option("preconditions")) 
  {
    report_statistics();
    report_preconditions();
    return property_checkert::UNKNOWN;
  }

  if(options.get_bool_option("termination")) 
  {
    report_statistics();
    property_checkert::resultt all_terminate = report_termination();
    return all_terminate;
  }

  property_checkert::resultt result =  check_properties(); 
  if(result==property_checkert::UNKNOWN) result = property_checkert::FAIL;
  report_statistics();
  return result;
}

/*******************************************************************\

Function: summary_checkert::SSA_functions

  Inputs:

 Outputs:

 Purpose:

\*******************************************************************/

void summary_checkert::SSA_functions(const goto_modelt &goto_model,  
				     const namespacet &ns)
{
  // compute SSA for all the functions
  forall_goto_functions(f_it, goto_model.goto_functions)
  {
    if(!f_it->second.body_available) continue;

    status() << "Computing SSA of " << f_it->first << messaget::eom;
    
    ssa_db.create(f_it->first, f_it->second, ns);
    local_SSAt &SSA = ssa_db.get(f_it->first);
    
    // simplify, if requested
    if(simplify)
    {
      status() << "Simplifying" << messaget::eom;
      ::simplify(SSA, ns);
    }
    SSA.output(debug()); debug() << eom;
  }

  ssa_unwinder.init(options.get_bool_option("k-induction"),
		    options.get_bool_option("incremental-bmc"));

  unsigned unwind = options.get_unsigned_int_option("unwind");
  if(!options.get_bool_option("k-induction") && 
     !options.get_bool_option("incremental-bmc") && unwind>0)
  {
    status() << "Unwinding" << messaget::eom;

    ssa_unwinder.init_localunwinders();

    ssa_unwinder.unwind_all(unwind+1);
    ssa_unwinder.output(debug()); debug() <<eom;
  }

  // properties
  initialize_property_map(goto_model.goto_functions);
}

/*******************************************************************\

Function: summary_checkert::summarize

  Inputs:

 Outputs:

 Purpose:

\*******************************************************************/

void summary_checkert::summarize(const goto_modelt &goto_model, 
				 bool forward,
				 bool termination)
{    
  summarizer_baset *summarizer;

  if(forward && !termination)
    summarizer = new summarizer_fwt(
      options,summary_db,ssa_db,ssa_unwinder,ssa_inliner);
  if(forward && termination)
    summarizer = new summarizer_fw_termt(
      options,summary_db,ssa_db,ssa_unwinder,ssa_inliner);
   if(!forward && !termination)
    summarizer = new summarizer_bwt(
      options,summary_db,ssa_db,ssa_unwinder,ssa_inliner);
  /* 
   if(!forward && termination)
    summarizer = new summarizer_bw_termt(
      options,summary_db,ssa_db,ssa_unwinder,ssa_inliner);
  */

  assert(summarizer != NULL);

  summarizer->set_message_handler(get_message_handler());

  if(!options.get_bool_option("context-sensitive"))
    summarizer->summarize();
  else
    summarizer->summarize(goto_model.goto_functions.entry_point());

  //statistics
  solver_instances += summarizer->get_number_of_solver_instances();
  solver_calls += summarizer->get_number_of_solver_calls();
  summaries_used += summarizer->get_number_of_summaries_used();

  delete summarizer;
}

/*******************************************************************\

Function: summary_checkert::check_properties

  Inputs:

 Outputs:

 Purpose:

\*******************************************************************/

summary_checkert::resultt summary_checkert::check_properties()
{
  // analyze all the functions
  for(ssa_dbt::functionst::const_iterator f_it = ssa_db.functions().begin();
      f_it != ssa_db.functions().end(); f_it++)
  {
    status() << "Checking properties of " << f_it->first << messaget::eom;
    if(options.get_bool_option("incremental"))
      check_properties_incremental(f_it);
    else
      check_properties_non_incremental(f_it);
  }
  
  summary_checkert::resultt result = property_checkert::PASS;
  for(property_mapt::const_iterator
      p_it=property_map.begin(); p_it!=property_map.end(); p_it++)
  {
    if(p_it->second.result==FAIL)
      return property_checkert::FAIL;
    if(p_it->second.result==UNKNOWN)
      result = property_checkert::UNKNOWN;
  }
    
  return result;
}

/*******************************************************************\

Function: summary_checkert::check_properties

  Inputs:

 Outputs:

 Purpose:

\*******************************************************************/

void summary_checkert::check_properties_non_incremental(
   const ssa_dbt::functionst::const_iterator f_it)
{
  local_SSAt &SSA = *f_it->second;
  if(!SSA.goto_function.body.has_assertion()) return;

  SSA.output(debug()); debug() << eom;
  
  // non-incremental version

  // solver
  incremental_solvert &solver = ssa_db.get_solver(f_it->first);
  solver.set_message_handler(get_message_handler());

  // give SSA to solver
  solver << SSA;
  SSA.mark_nodes();

  const goto_programt &goto_program=SSA.goto_function.body;

  for(goto_programt::instructionst::const_iterator
      i_it=goto_program.instructions.begin();
      i_it!=goto_program.instructions.end();
      i_it++)
  {
    if(!i_it->is_assert())
      continue;
  
    const source_locationt &source_location=i_it->source_location;  
    std::list<local_SSAt::nodest::const_iterator> assertion_nodes;
    SSA.find_nodes(i_it,assertion_nodes);

    irep_idt property_id=source_location.get_property_id();

    if(property_id=="") //TODO: some properties do not show up in initialize_property_map
      continue;     

    //do not recheck properties that have already been decided
    if(property_map[property_id].result!=UNKNOWN) continue; 

    property_map[property_id].location = i_it;
    
    exprt::operandst conjuncts;
    for(std::list<local_SSAt::nodest::const_iterator>::const_iterator
	  n_it=assertion_nodes.begin();
        n_it!=assertion_nodes.end();
        n_it++)
    {
      for(local_SSAt::nodet::assertionst::const_iterator
	    a_it=(*n_it)->assertions.begin();
	  a_it!=(*n_it)->assertions.end();
	  a_it++)
      {
	conjuncts.push_back(*a_it);

	if(show_vcc)
	{
	  do_show_vcc(SSA, i_it, a_it);
	  continue;
	}
      }
    }
    exprt property = not_exprt(conjunction(conjuncts));
    if(simplify)
      property=::simplify_expr(property, SSA.ns);
  
    solver.new_context();
    solver << SSA.get_enabling_exprs();
    
    // invariant, calling contexts
    if(summary_db.exists(f_it->first))
    {
      solver << summary_db.get(f_it->first).fw_invariant;
      solver << summary_db.get(f_it->first).fw_precondition;
    }

    //callee summaries
    solver << ssa_inliner.get_summaries(SSA);

    // give negated property to solver
    solver << property;

    //freeze loop head selects
    exprt::operandst loophead_selects = 
      get_loophead_selects(f_it->first,SSA,solver.solver);

    // solve
    switch(solver())
      {
      case decision_proceduret::D_SATISFIABLE: 
      {
	if(options.get_bool_option("spurious-check"))
	{
	  bool spurious = is_spurious(loophead_selects,solver) ;
	  debug() << "[" << property_id << "] is " << 
	    (spurious ? "" : "not ") << "spurious" << eom;

	  property_map[property_id].result = spurious ? UNKNOWN : FAIL;

	  if(!spurious)
	  {
	    show_error_trace(f_it->first,SSA,solver.solver,
			     debug(),get_message_handler());
	  }
	}
	break; 
      }
     
      case decision_proceduret::D_UNSATISFIABLE:
	property_map[property_id].result=PASS;
	break;

      case decision_proceduret::D_ERROR:    
      default:
	property_map[property_id].result=ERROR;
	throw "error from decision procedure";
      }
    solver.pop_context();
  }
} 

/*******************************************************************\

Function: summary_checkert::check_properties_incremental

  Inputs:

 Outputs:

 Purpose:

\*******************************************************************/

void summary_checkert::check_properties_incremental(
   const ssa_dbt::functionst::const_iterator f_it)
{
  local_SSAt &SSA = *f_it->second;
  if(!SSA.goto_function.body.has_assertion()) return;

  SSA.output(debug()); debug() << eom;
  
  // incremental version

  // solver
  incremental_solvert &solver = ssa_db.get_solver(f_it->first);
  solver.set_message_handler(get_message_handler());

  // give SSA to solver
  solver << SSA;
  SSA.mark_nodes();

  solver.new_context();

  exprt enabling_expr = SSA.get_enabling_exprs();
  solver << enabling_expr;

  // invariant, calling contexts
  if(summary_db.exists(f_it->first))
  {
    solver << summary_db.get(f_it->first).fw_invariant;
    solver << summary_db.get(f_it->first).fw_precondition;
  }

  //callee summaries
  solver << ssa_inliner.get_summaries(SSA);

  //freeze loop head selects
  exprt::operandst loophead_selects = 
    get_loophead_selects(f_it->first,SSA,solver.solver);

  cover_goals_extt cover_goals(solver,loophead_selects,property_map,
			       options.get_bool_option("spurious-check"));

#if 0   
  debug() << "(C) " << from_expr(SSA.ns,"",enabling_expr) << eom;
#endif

  const goto_programt &goto_program=SSA.goto_function.body;

  for(goto_programt::instructionst::const_iterator
      i_it=goto_program.instructions.begin();
      i_it!=goto_program.instructions.end();
      i_it++)
  {
    if(!i_it->is_assert())
      continue;
  
    const locationt &location=i_it->source_location;  
    std::list<local_SSAt::nodest::const_iterator> assertion_nodes;
    SSA.find_nodes(i_it,assertion_nodes);

    irep_idt property_id = location.get_property_id();

    //do not recheck properties that have already been decided
    if(property_map[property_id].result!=UNKNOWN) continue; 

    if(property_id=="") //TODO: some properties do not show up in initialize_property_map
      continue;     

    unsigned property_counter = 0;
    for(std::list<local_SSAt::nodest::const_iterator>::const_iterator
	  n_it=assertion_nodes.begin();
        n_it!=assertion_nodes.end();
        n_it++)
    {
      for(local_SSAt::nodet::assertionst::const_iterator
	    a_it=(*n_it)->assertions.begin();
	  a_it!=(*n_it)->assertions.end();
	  a_it++, property_counter++)
      {
	exprt property=*a_it;

	if(simplify)
	  property=::simplify_expr(property, SSA.ns);

#if 0 
	std::cout << "property: " << from_expr(SSA.ns, "", property) << std::endl;
#endif
 
	property_map[property_id].location = i_it;
	cover_goals.goal_map[property_id].conjuncts.push_back(property);
      }
    }
  }
    
  for(cover_goals_extt::goal_mapt::const_iterator
      it=cover_goals.goal_map.begin();
      it!=cover_goals.goal_map.end();
      it++)
  {
    // Our goal is to falsify a property.
    // The following is TRUE if the conjunction is empty.
    literalt p=!solver.solver.convert(conjunction(it->second.conjuncts));
    cover_goals.add(p);
  }

  status() << "Running " << solver.solver.decision_procedure_text() << eom;

  cover_goals();  

  std::list<cover_goals_extt::cover_goalt>::const_iterator g_it=
    cover_goals.goals.begin();
  for(cover_goals_extt::goal_mapt::const_iterator
      it=cover_goals.goal_map.begin();
      it!=cover_goals.goal_map.end();
      it++, g_it++)
  {
    if(!g_it->covered) property_map[it->first].result=PASS;
  }

  solver.pop_context();

  debug() << "** " << cover_goals.number_covered()
           << " of " << cover_goals.size() << " failed ("
           << cover_goals.iterations() << " iterations)" << eom;
} 

/*******************************************************************\

Function: summary_checkert::report_statistics()

  Inputs:

 Outputs:

 Purpose:

\*******************************************************************/

void summary_checkert::report_statistics()
{
  for(ssa_dbt::functionst::const_iterator f_it = ssa_db.functions().begin();
	f_it != ssa_db.functions().end(); f_it++)
  {
    incremental_solvert &solver = ssa_db.get_solver(f_it->first);
    unsigned calls = solver.get_number_of_solver_calls();
    if(calls>0) solver_instances++;
    solver_calls += calls;
  }
  statistics() << "** statistics: " << eom;
  statistics() << "  number of solver instances: " << solver_instances << eom;
  statistics() << "  number of solver calls: " << solver_calls << eom;
  statistics() << "  number of summaries used: " << summaries_used << eom;
  statistics() << eom;
}
  
/*******************************************************************\

Function: summary_checkert::do_show_vcc

  Inputs:

 Outputs:

 Purpose:

\*******************************************************************/

void summary_checkert::do_show_vcc(
  const local_SSAt &SSA,
  const goto_programt::const_targett i_it,
  const local_SSAt::nodet::assertionst::const_iterator &a_it)
{
  std::cout << i_it->source_location << "\n";
  std::cout << i_it->source_location.get_comment() << "\n";
  
  std::list<exprt> ssa_constraints;
  ssa_constraints << SSA;

  unsigned i=1;
  for(std::list<exprt>::const_iterator c_it=ssa_constraints.begin();
      c_it!=ssa_constraints.end();
      c_it++, i++)
    std::cout << "{-" << i << "} " << from_expr(SSA.ns, "", *c_it) << "\n";

  std::cout << "|--------------------------\n";
  
  std::cout << "{1} " << from_expr(SSA.ns, "", *a_it) << "\n";
  
  std::cout << "\n";
}

/*******************************************************************\

Function: summary_checkert::report_preconditions

  Inputs:

 Outputs:

 Purpose:

\*******************************************************************/

void summary_checkert::report_preconditions()
{
  result() << eom;
  result() << "** Preconditions: " << eom;
  ssa_dbt::functionst &functions = ssa_db.functions();
  for(ssa_dbt::functionst::iterator it = functions.begin();
      it != functions.end(); it++)
  {
    exprt precondition;
    bool computed = summary_db.exists(it->first);
    if(computed) precondition = summary_db.get(it->first).bw_precondition;
    result() << eom << "[" << it->first << "]: " 
	     << (!computed ? "not computed" : 
		 from_expr(it->second->ns, "", precondition)) << eom;
  }
}

/*******************************************************************\

Function: summary_checkert::report_termination

  Inputs:

 Outputs:

 Purpose:

\*******************************************************************/

property_checkert::resultt summary_checkert::report_termination()
{
  result() << eom;
  result() << "** Termination: " << eom;
  bool all_terminate = true; 
  ssa_dbt::functionst &functions = ssa_db.functions();
  for(ssa_dbt::functionst::iterator it = functions.begin();
      it != functions.end(); it++)
  {
    threevalt terminates = YES;
    bool computed = summary_db.exists(it->first);
    if(computed) terminates = summary_db.get(it->first).terminates;
    all_terminate = all_terminate && (terminates==YES);
    result() << "[" << it->first << "]: " 
	     << (!computed ? "not computed" : threeval2string(terminates)) << eom;
  }
  if(all_terminate) return property_checkert::PASS;
  return property_checkert::FAIL;
}

/*******************************************************************\

Function: summary_checkert::is_spurious

  Inputs:

 Outputs:

 Purpose: checks whether a countermodel is spurious

\*******************************************************************/

exprt::operandst summary_checkert::get_loophead_selects(
  const irep_idt &function_name, 
  const local_SSAt &SSA, prop_convt &solver)
{
  exprt::operandst loophead_selects;
  for(local_SSAt::nodest::const_iterator n_it = SSA.nodes.begin();
      n_it != SSA.nodes.end(); n_it++)
  {
    if(n_it->loophead==SSA.nodes.end()) continue;
    symbol_exprt lsguard = SSA.name(SSA.guard_symbol(), local_SSAt::LOOP_SELECT, n_it->location);
    ssa_unwinder.get(function_name).unwinder_rename(lsguard,*n_it,true);
    loophead_selects.push_back(not_exprt(lsguard));
    solver.set_frozen(solver.convert(lsguard));
  }
  literalt loophead_selects_literal = solver.convert(conjunction(loophead_selects));
  if(!loophead_selects_literal.is_constant())
    solver.set_frozen(loophead_selects_literal);

  //  std::cout << "loophead_selects: " << from_expr(SSA.ns,"",conjunction(loophead_selects)) << std::endl;

  return loophead_selects;
}

/*******************************************************************\

Function: summary_checkert::is_spurious

  Inputs:

 Outputs:

 Purpose: checks whether a countermodel is spurious

\*******************************************************************/

bool summary_checkert::is_spurious(const exprt::operandst &loophead_selects, 
				   incremental_solvert &solver)
{
  //check loop head choices in model
  bool invariants_involved = false;
  for(exprt::operandst::const_iterator l_it = loophead_selects.begin();
        l_it != loophead_selects.end(); l_it++)
  {
    if(solver.solver.get(l_it->op0()).is_true()) 
    {
      invariants_involved = true; 
      break;
    }
  }
  if(!invariants_involved) return false;
  
  // force avoiding paths going through invariants
  solver << conjunction(loophead_selects);

  solver_calls++; //statistics

  switch(solver())
  {
  case decision_proceduret::D_SATISFIABLE:
    return false;
    break;
      
  case decision_proceduret::D_UNSATISFIABLE:
    return true;
    break;

  case decision_proceduret::D_ERROR:    
  default:
    throw "error from decision procedure";
  }
}
