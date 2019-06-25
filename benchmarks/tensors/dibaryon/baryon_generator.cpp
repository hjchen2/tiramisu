#include <tiramisu/tiramisu.h>
#include <string.h>
#include "baryon_wrapper.h"

//#define FUSE 1
#define PARALLEL 0

using namespace tiramisu;

/**
  * Multiply the two complex numbers p1 and p2 and return the real part.
  */
expr mul_r(std::pair<expr, expr> p1, std::pair<expr, expr> p2)
{
    expr e1 = (p1.first * p2.first);
    expr e2 = (p1.second * p2.second);
    return (e1 - e2);
}

/**
  * Multiply the two complex numbers p1 and p2 and return the imaginary part.
  */
expr mul_i(std::pair<expr, expr> p1, std::pair<expr, expr> p2)
{
    expr e1 = (p1.first * p2.second);
    expr e2 = (p1.second * p2.first);
    return (e1 + e2);
}

template<typename ... Args>
std::string str_fmt( const std::string& format, Args ... args )
{
  size_t size = snprintf( nullptr, 0, format.c_str(), args ... ) + 1; // Extra space for '\0'
  std::unique_ptr<char[]> buf( new char[ size ] ); 
  snprintf( buf.get(), size, format.c_str(), args ... );
  return std::string( buf.get(), buf.get() + size - 1 ); // We don't want the '\0' inside
}

// replace all occurence of `target` with `replacement` in `e`
expr replace(expr e, expr target, expr replacement) {
  if (e.is_equal(target)) {
    return replacement;
  }

  auto recur_on_operand = [&target, &replacement](const expr &operand) -> expr {
    replace(operand, target, replacement);
  };

  expr ep = e.apply_to_operands(recur_on_operand);
  return ep;
}

// given a list of potentially redundant loads:
// replace use of these loads with lets,
//
// return the lets introduced, which are not scheduled (the user should schedule it)
std::vector<computation *> eliminate_redundant_loads(
    std::map<std::string, expr> loads,/* loads that are potentially redundant */
    std::vector<computation *> computations/* computations that we want to optimize */,
    int loop_level) {
  assert(!computations.empty());
  std::vector<computation *> let_stmts;
  for (auto &nameAndLoad : loads) {
    std::string name;
    expr load;
    std::tie(name, load) = nameAndLoad;
    auto *let_stmt = new constant(
        name, load, load.get_data_type(),
        false/*whether invariant across whole function*/,
        computations[0]/*compute this constant with this*/,
        loop_level);
    for (auto *user : computations) {
      expr new_user_expr = replace(user->get_expr(), load, *let_stmt);
      user->set_expression(new_user_expr);
    }
    let_stmts.push_back(let_stmt);
  }
  return let_stmts;
}

/*
 * The goal is to generate code that implements the reference.
 * baryon_ref.cpp
 */
void generate_function(std::string name)
{
    tiramisu::init(name);

    var n("n", 0, Nsrc),
	iCprime("iCprime", 0, Nc),
	iSprime("iSprime", 0, Ns),
	jCprime("jCprime", 0, Nc),
	jSprime("jSprime", 0, Ns),
	kCprime("kCprime", 0, Nc),
	kSprime("kSprime", 0, Ns),
	lCprime("lCprime", 0, Nc),
	lSprime("lSprime", 0, Ns),
	x("x", 0, Vsnk),
	x2("x2", 0, Vsnk),
	t("t", 0, Lt),
	y("y", 0, Vsrc),
	tri("tri", 0, Nq);

    input Blocal_r("Blocal_r", {t, n, iCprime, iSprime, jCprime, jSprime, kCprime, kSprime, x}, p_float64);
    input Blocal_i("Blocal_i", {t, n, iCprime, iSprime, jCprime, jSprime, kCprime, kSprime, x}, p_float64);
    input   prop_r("prop_r",   {t, tri, iCprime, iSprime, jCprime, jSprime, x, y}, p_float64);
    input   prop_i("prop_i",   {t, tri, iCprime, iSprime, jCprime, jSprime, x, y}, p_float64);
    //input  weights("weights",  {wnum}, p_float64);
    input    psi_r("psi_r",    {n, y}, p_float64);
    input    psi_i("psi_i",    {n, y}, p_float64);
    //input    color_weights("color_weights",    {wnum, tri}, p_int32);
    //input    spin_weights("spin_weights",    {wnum, tri}, p_int32);

    computation Blocal_r_init("Blocal_r_init", {t, n, iCprime, iSprime, jCprime, jSprime, kCprime, kSprime, x}, expr((double) 0));
    computation Blocal_i_init("Blocal_i_init", {t, n, iCprime, iSprime, jCprime, jSprime, kCprime, kSprime, x}, expr((double) 0));

    ////////////////// BEGIN TOM's STUFF   //////////////////
    std::map<std::string, expr> prop_loads;
    std::map<std::pair<int,int>, std::pair<expr, expr>> Q2_exprs;
    for (int ii = 0; ii < Nw; ii++) {
      int ic = test_color_weights[ii][0];
      int is = test_spin_weights[ii][0];
      int jc = test_color_weights[ii][1];
      int js = test_spin_weights[ii][1];
      int kc = test_color_weights[ii][2];
      int ks = test_spin_weights[ii][2];
      std::pair<expr, expr> prop_0(
          prop_r(t, 0, iCprime, iSprime, ic, is, x, y),
          prop_i(t, 0, iCprime, iSprime, ic, is, x, y));
      std::pair<expr, expr> prop_2(
          prop_r(t, 2, kCprime, kSprime, kc, ks, x, y),
          prop_i(t, 2, kCprime, kSprime, kc, ks, x, y));
      std::pair<expr, expr> prop_0p(
          prop_r(t, 0, kCprime, kSprime, ic, is, x, y), 
          prop_i(t, 0, kCprime, kSprime, ic, is, x, y));
      std::pair<expr, expr> prop_2p(
          prop_r(t, 2, iCprime, iSprime, kc, ks, x, y),
          prop_i(t, 2, iCprime, iSprime, kc, ks, x, y));
      // remember which loads we are doing.
      // later we will elimnate redundant loads within the inner loop body
      prop_loads[str_fmt("prop_r_load_0_%d_%d_i", ic, is)] = prop_0.first;
      prop_loads[str_fmt("prop_i_load_0_%d_%d_i", ic, is)] = prop_0.second;
      prop_loads[str_fmt("prop_r_load_2_%d_%d_k", kc, ks)] = prop_2.first;
      prop_loads[str_fmt("prop_i_load_2_%d_%d_k", kc, ks)] = prop_2.second;
      prop_loads[str_fmt("prop_r_load_0_%d_%d_k", ic, is)] = prop_0p.first;
      prop_loads[str_fmt("prop_i_load_0_%d_%d_k", ic, is)] = prop_0p.second;
      prop_loads[str_fmt("prop_r_load_2_%d_%d_i", kc, ks)] = prop_2p.first;
      prop_loads[str_fmt("prop_i_load_2_%d_%d_i", kc, ks)] = prop_2p.second;
      
      expr real = (mul_r(prop_0, prop_2) - mul_r(prop_0p, prop_2p)) * test_weights[ii];
      expr imag = (mul_i(prop_0, prop_2) - mul_i(prop_0p, prop_2p)) * test_weights[ii];
      auto it = Q2_exprs.find({jc, js});
      if (it == Q2_exprs.end()) {
        Q2_exprs[{jc,js}] = {real, imag};
        continue;
      }
      expr real_acc = it->second.first;
      expr imag_acc = it->second.second;
      it->second.first = real_acc.copy() + real.copy();
      it->second.second = imag_acc.copy() + imag.copy();
    }
    std::map<std::pair<int,int>, std::pair<computation *, computation *>> Q2;
    for (auto &jAndExpr : Q2_exprs) {
      int jc, js;
      expr real, imag;
      std::tie(jc, js) = jAndExpr.first;
      std::tie(real, imag) = jAndExpr.second;
      auto *realComputation = new computation(
          // name
          str_fmt("q_%d_%d_r", jc, js),
          // iterators
          { t, iCprime, iSprime, kCprime, kSprime, x, y },
          // definition
          real);
      auto *imagComputation = new computation(
          // name
          str_fmt("q_%d_%d_i", jc, js),
          // iterators
          { t, iCprime, iSprime, kCprime, kSprime, x, y },
          // definition
          imag);
      //realComputation->add_predicate(iCprime != kCprime || iSprime != kSprime);
      //imagComputation->add_predicate(iCprime != kCprime || iSprime != kSprime);
      Q2[{jc,js}].first = realComputation;
      Q2[{jc,js}].second = imagComputation;
    }
    ////////////////// END TOM's STUFF   //////////////////

    //std::pair<expr, expr> prop_0(prop_r(t, 0, iCprime, iSprime, color_weights(wnum, 0), spin_weights(wnum, 0), x, y), prop_i(t, 0, iCprime, iSprime, color_weights(wnum, 0), spin_weights(wnum, 0), x, y));
    //std::pair<expr, expr> prop_2(prop_r(t, 2, kCprime, kSprime, color_weights(wnum, 2), spin_weights(wnum, 2), x, y), prop_i(t, 2, kCprime, kSprime, color_weights(wnum, 2), spin_weights(wnum, 2), x, y));
    //std::pair<expr, expr> prop_0p(prop_r(t, 0, kCprime, kSprime, color_weights(wnum, 0), spin_weights(wnum, 0), x, y), prop_i(t, 0, kCprime, kSprime, color_weights(wnum, 0), spin_weights(wnum, 0), x, y));
    //std::pair<expr, expr> prop_2p(prop_r(t, 2, iCprime, iSprime, color_weights(wnum, 2), spin_weights(wnum, 2), x, y), prop_i(t, 2, iCprime, iSprime, color_weights(wnum, 2), spin_weights(wnum, 2), x, y));
    //std::pair<expr, expr> m1(mul_r(prop_0, prop_2) - mul_r(prop_0p, prop_2p), mul_i(prop_0, prop_2) - mul_i(prop_0p, prop_2p));
    std::pair<expr, expr> psi(psi_r(n, y), psi_i(n, y));
    //std::pair<expr, expr> m2(mul_r(psi, m1), mul_i(psi, m1));
    //expr prop_r_1 = prop_r(t, 1, jCprime, jSprime, color_weights(wnum, 1), spin_weights(wnum, 1), x, y);
    //expr prop_i_1 = prop_i(t, 1, jCprime, jSprime, color_weights(wnum, 1), spin_weights(wnum, 1), x, y);
    //std::pair<expr, expr> prop_1(prop_r_1, prop_i_1);

#if 0
    computation Blocal_r_update("Blocal_r_update", {t, n, iCprime, iSprime, jCprime, jSprime, kCprime, kSprime, x, y, wnum}, p_float64);
    Blocal_r_update.set_expression(Blocal_r_init(t, n, iCprime, iSprime, jCprime, jSprime, kCprime, kSprime, x) + weights(wnum) * mul_r(m2, prop_1));

    computation Blocal_i_update("Blocal_i_update", {t, n, iCprime, iSprime, jCprime, jSprime, kCprime, kSprime, x, y, wnum}, p_float64);
    Blocal_i_update.set_expression(Blocal_i_init(t, n, iCprime, iSprime, jCprime, jSprime, kCprime, kSprime, x) + weights(wnum) * mul_i(m2, prop_1));
#endif

    //computation Q_r_init("Q_r_init", {t, n, iCprime, iSprime, kCprime, kSprime, jCprime, jSprime, x, y}, expr((double) 0));
    //computation Q_i_init("Q_i_init", {t, n, iCprime, iSprime, kCprime, kSprime, jCprime, jSprime, x, y}, expr((double) 0));

    computation Bsingle_r_init("Bsingle_r_init", {t, n, iCprime, iSprime, kCprime, kSprime, jCprime, jSprime, x, x2}, expr((double) 0));
    computation Bsingle_i_init("Bsingle_i_init", {t, n, iCprime, iSprime, kCprime, kSprime, jCprime, jSprime, x, x2}, expr((double) 0));

    std::vector<std::pair<computation *, computation *>> Bsingle_updates;
    std::vector<std::pair<computation *, computation *>> Blocal_updates;
    struct Q2UserEdge {
      computation *q_r, *q_i,
                  *bs_r, *bs_i,
                  *bl_r, *bl_i;
    };
    std::vector<Q2UserEdge> q2userEdges;
    for (auto &jAndComp : Q2) {
      int jc, js;
      computation *q_real;
      computation *q_imag;
      std::tie(jc, js) = jAndComp.first;
      std::tie(q_real, q_imag) = jAndComp.second;

      // iterators : { t, iCprime, iSprime, kCprime, kSprime, x, y },
      std::pair<expr, expr> q(
          (*q_real)(t, iCprime, iSprime, kCprime, kSprime, x, y),
          (*q_imag)(t, iCprime, iSprime, kCprime, kSprime, x, y));
      //std::pair<expr, expr> q(
      //    q_real->get_expr().copy(),
      //    q_imag->get_expr().copy());

      // NOTE
      // iterators of Q { t, iCprime, iSprime, kCprime, kSprime, x, y },
      //

      // ------ COMPUTE BSINGLE ------
      std::pair<expr, expr> prop1_x2(
          prop_r(t, 1, jCprime, jSprime, jc, js, x2, y),
          prop_i(t, 1, jCprime, jSprime, jc, js, x2, y));
      std::pair<expr, expr> mul_with_prop1_x2(
          mul_r(q, prop1_x2),
          mul_i(q, prop1_x2));
      expr bsingle_init_r = Bsingle_r_init(t, n, iCprime, iSprime, jCprime, jSprime, kCprime, kSprime, x, x2);
      expr bsingle_init_i = Bsingle_i_init(t, n, iCprime, iSprime, jCprime, jSprime, kCprime, kSprime, x, x2);
      expr bsingle_update_r = bsingle_init_r + mul_r(mul_with_prop1_x2, psi);
      expr bsingle_update_i = bsingle_init_i + mul_i(mul_with_prop1_x2, psi);
      auto *bsingle_r = new computation(
          // name
          str_fmt("bsingle_update_%d_%d_r", jc, js),
          // iterator
          {t, iCprime, iSprime, kCprime, kSprime, x, n, jCprime, jSprime, x2, y},
          // definition
          bsingle_update_r);
      auto *bsingle_i = new computation(
          // name
          str_fmt("bsingle_update_%d_%d_i", jc, js),
          // iterator
          {t, iCprime, iSprime, kCprime, kSprime, x, n, jCprime, jSprime, x2, y},
          // definition
          bsingle_update_i);
      bsingle_r->add_predicate(iCprime != kCprime || iSprime != kSprime);
      bsingle_i->add_predicate(iCprime != kCprime || iSprime != kSprime);
      Bsingle_updates.push_back({bsingle_r, bsingle_i});

      // ------- COMPUTE BLOCAL ---------
      std::pair<expr, expr> prop1_x(
          prop_r(t, 1, jCprime, jSprime, jc, js, x, y),
          prop_i(t, 1, jCprime, jSprime, jc, js, x, y));
      std::pair<expr, expr> mul_with_prop1_x(
          mul_r(q, prop1_x),
          mul_i(q, prop1_x));
      expr blocal_init_r = Blocal_r_init(t, n, iCprime, iSprime, jCprime, jSprime, kCprime, kSprime, x);
      expr blocal_init_i = Blocal_i_init(t, n, iCprime, iSprime, jCprime, jSprime, kCprime, kSprime, x);
      expr blocal_update_r = blocal_init_r + mul_r(mul_with_prop1_x, psi);
      expr blocal_update_i = blocal_init_i + mul_i(mul_with_prop1_x, psi);
      auto *blocal_r = new computation(
          // name
          str_fmt("blocal_update_%d_%d_r", jc, js),
          // iterator
          {t, iCprime, iSprime, kCprime, kSprime, x, n, jCprime, jSprime, y},
          // definition
          blocal_update_r);
      auto *blocal_i = new computation(
          // name
          str_fmt("blocal_update_%d_%d_i", jc, js),
          // iterator
          {t, iCprime, iSprime, kCprime, kSprime, x, n, jCprime, jSprime, y},
          // definition
          blocal_update_i);
      Blocal_updates.push_back({blocal_r, blocal_i});
      blocal_r->add_predicate(iCprime != kCprime || iSprime != kSprime);
      blocal_i->add_predicate(iCprime != kCprime || iSprime != kSprime);

      Q2UserEdge edge {q_real, q_imag, bsingle_r, bsingle_i, blocal_r, blocal_i};
      q2userEdges.push_back(edge);
    }

#define BLOCK_SIZE 3
    // block every 3 together
    std::vector<std::vector<Q2UserEdge>> blocked_q2userEdges;
    for (int ii = 0; ii < q2userEdges.size(); ii += BLOCK_SIZE) {
      std::vector<Q2UserEdge> blocked_edge;
      for (int i = ii; i < q2userEdges.size() && i < ii+BLOCK_SIZE; i++) {
        blocked_edge.push_back(q2userEdges[i]);
      }
      blocked_q2userEdges.push_back(blocked_edge);
    }

#if 0
    computation Q_r_update("Q_r_update", {t, n, iCprime, iSprime, kCprime, kSprime, jCprime, jSprime, x, y, wnum},
			Q_r_init(t, n, iCprime, iSprime, kCprime, kSprime, color_weights(wnum, 1), spin_weights(wnum, 1), x, y) + weights(wnum) * mul_r(psi, m1));
    Q_r_update.add_predicate((jCprime == color_weights(wnum, 1)) && (jSprime == spin_weights(wnum, 1)));

    computation Q_i_update("Q_i_update", {t, n, iCprime, iSprime, kCprime, kSprime, jCprime, jSprime, x, y, wnum},
			Q_i_init(t, n, iCprime, iSprime, kCprime, kSprime, color_weights(wnum, 1), spin_weights(wnum, 1), x, y) + weights(wnum) * mul_i(psi, m1));
    Q_i_update.add_predicate((jCprime == color_weights(wnum, 1)) && (jSprime == spin_weights(wnum, 1)));

    std::pair<expr, expr> Q_update(Q_r_update(t, n, iCprime, iSprime, kCprime, kSprime, lCprime, lSprime, x, y, wnum), Q_i_update(t, n, iCprime, iSprime, kCprime, kSprime, lCprime, lSprime, x, y, wnum));
    std::pair<expr, expr> prop_1p(prop_r(t, 1, jCprime, jSprime, lCprime, lSprime, x2, y), prop_i(t, 1, jCprime, jSprime, lCprime, lSprime, x2, y));

    computation Bsingle_r_update("Bsingle_r_update", {t, n, iCprime, iSprime, jCprime, jSprime, kCprime, kSprime, lCprime, lSprime, x, x2, y},
	    Bsingle_r_init(t, n, iCprime, iSprime, jCprime, jSprime, kCprime, kSprime, x, x2) + mul_r(Q_update, prop_1p));

    computation Bsingle_i_update("Bsingle_i_update", {t, n, iCprime, iSprime, jCprime, jSprime, kCprime, kSprime, lCprime, lSprime, x, x2, y},
	    Bsingle_i_init(t, n, iCprime, iSprime, jCprime, jSprime, kCprime, kSprime, x, x2) + mul_i(Q_update, prop_1p));
#endif


#if 0
    computation Bdouble_r_init("Bdouble_r_init", {t, n, iCprime, iSprime, jCprime, jSprime, kCprime, kSprime, x, x2}, expr((double) 0));
    computation Bdouble_i_init("Bdouble_i_init", {t, n, iCprime, iSprime, jCprime, jSprime, kCprime, kSprime, x, x2}, expr((double) 0));

    computation O_r_init("O_r_init", {t, n, jCprime, jSprime, kCprime, kSprime, iCprime, iSprime, x, y}, expr((double) 0));
    computation O_i_init("O_i_init", {t, n, jCprime, jSprime, kCprime, kSprime, iCprime, iSprime, x, y}, expr((double) 0));

    computation P_r_init("P_r_init", {t, n, jCprime, jSprime, kCprime, kSprime, iCprime, iSprime, x, y}, expr((double) 0));
    computation P_i_init("P_i_init", {t, n, jCprime, jSprime, kCprime, kSprime, iCprime, iSprime, x, y}, expr((double) 0));

    std::pair<expr, expr> m3(mul_r(psi, prop_1), mul_i(psi, prop_1));
    computation O_r_update("O_r_update", {t, n, jCprime, jSprime, kCprime, kSprime, iCprime, iSprime, x, y, wnum},
			O_r_init(t, n, jCprime, jSprime, kCprime, kSprime, color_weights(wnum, 0), spin_weights(wnum, 0), x, y) + weights(wnum) * mul_r(m3, prop_2));
    O_r_update.add_predicate((iCprime == color_weights(wnum, 0)) && (iSprime == spin_weights(wnum, 0)));

    computation O_i_update("O_i_update", {t, n, jCprime, jSprime, kCprime, kSprime, iCprime, iSprime, x, y, wnum},
			O_i_init(t, n, jCprime, jSprime, kCprime, kSprime, color_weights(wnum, 0), spin_weights(wnum, 0), x, y) + weights(wnum) * mul_i(m3, prop_2));
    O_i_update.add_predicate((iCprime == color_weights(wnum, 0)) && (iSprime == spin_weights(wnum, 0)));

    std::pair<expr, expr> m4(mul_r(psi, prop_0p), mul_i(psi, prop_0p));
    computation P_r_update("P_r_update", {t, n, jCprime, jSprime, kCprime, kSprime, iCprime, iSprime, x, y, wnum},
			P_r_init(t, n, jCprime, jSprime, kCprime, kSprime, color_weights(wnum, 2), spin_weights(wnum, 2), x, y) + weights(wnum) * mul_r(m4, prop_1));
    P_r_update.add_predicate((iCprime == color_weights(wnum, 2)) && (iSprime == spin_weights(wnum, 2)));

    computation P_i_update("P_i_update", {t, n, jCprime, jSprime, kCprime, kSprime, iCprime, iSprime, x, y, wnum},
			P_i_init(t, n, jCprime, jSprime, kCprime, kSprime, color_weights(wnum, 2), spin_weights(wnum, 2), x, y) + weights(wnum) * mul_i(m4, prop_1));
    P_i_update.add_predicate((iCprime == color_weights(wnum, 2)) && (iSprime == spin_weights(wnum, 2)));

    std::pair<expr, expr> O_update(O_r_update(t, n, jCprime, jSprime, kCprime, kSprime, lCprime, lSprime, x, y, wnum), O_i_update(t, n, jCprime, jSprime, kCprime, kSprime, lCprime, lSprime, x, y, wnum));
    std::pair<expr, expr> P_update(P_r_update(t, n, jCprime, jSprime, kCprime, kSprime, lCprime, lSprime, x, y, wnum), P_i_update(t, n, jCprime, jSprime, kCprime, kSprime, lCprime, lSprime, x, y, wnum));
    std::pair<expr, expr> prop_0pp(prop_r(t, 0, iCprime, iSprime, lCprime, lSprime, x2, y), prop_i(t, 0, iCprime, iSprime, lCprime, lSprime, x2, y));

    computation Bdouble_r_update0("Bdouble_r_update0", {t, n, iCprime, iSprime, jCprime, jSprime, kCprime, kSprime, lCprime, lSprime, x, x2, y},
	    Bdouble_r_init(t, n, iCprime, iSprime, jCprime, jSprime, kCprime, kSprime, x, x2) + mul_r(prop_0pp, O_update));

    computation Bdouble_i_update0("Bdouble_i_update0", {t, n, iCprime, iSprime, jCprime, jSprime, kCprime, kSprime, lCprime, lSprime, x, x2, y},
	    Bdouble_i_init(t, n, iCprime, iSprime, jCprime, jSprime, kCprime, kSprime, x, x2) + mul_i(prop_0pp, O_update));

    std::pair<expr, expr> prop_2pp(prop_r(t, 2, iCprime, iSprime, lCprime, lSprime, x2, y), prop_i(t, 2, iCprime, iSprime, lCprime, lSprime, x2, y));
    computation Bdouble_r_update1("Bdouble_r_update1", {t, n, iCprime, iSprime, jCprime, jSprime, kCprime, kSprime, lCprime, lSprime, x, x2, y},
	    Bdouble_r_init(t, n, iCprime, iSprime, jCprime, jSprime, kCprime, kSprime, x, x2) - mul_r(P_update, prop_2pp));

    computation Bdouble_i_update1("Bdouble_i_update1", {t, n, iCprime, iSprime, jCprime, jSprime, kCprime, kSprime, lCprime, lSprime, x, x2, y},
	    Bdouble_i_init(t, n, iCprime, iSprime, jCprime, jSprime, kCprime, kSprime, x, x2) - mul_i(P_update, prop_2pp));
#endif

    // -------------------------------------------------------
    // Layer II
    // -------------------------------------------------------
#if 0
    block init_blk({&Blocal_r_init, &Blocal_i_init, &Q_r_init, &Q_i_init,
		&Bsingle_r_init, &Bsingle_i_init, &Bdouble_r_init, &Bdouble_i_init,
		&O_r_init, &O_i_init, &P_r_init, &P_i_init});

    block Blocal_blk({&Blocal_r_update, &Blocal_i_update, &Q_r_update, &Q_i_update,
		 &O_r_update, &O_i_update, &P_r_update, &P_i_update});

    block Bsingle_blk({&Bsingle_r_update, &Bsingle_i_update, &Bdouble_r_update0,
			&Bdouble_i_update0, &Bdouble_r_update1, &Bdouble_i_update1});
#endif
    //block init_blk({&Q_r_init, &Q_i_init,
	//	&Bsingle_r_init, &Bsingle_i_init});

    //block Blocal_blk({&Q_r_update, &Q_i_update});

    //block Bsingle_blk({&Bsingle_r_update, &Bsingle_i_update});

#if FUSE
    Bsingle_blk.interchange(iCprime, jCprime);
    Bsingle_blk.interchange(iSprime, jSprime);
    Bsingle_blk.interchange(iCprime, kCprime);
    Bsingle_blk.interchange(iSprime, kSprime);
#endif

#if 0
    Blocal_r_init.then(Blocal_i_init, x)
		 .then(Q_r_init, computation::root)
		 .then(Q_i_init, y)
		 .then(Bsingle_r_init, x2)
		 .then(Bsingle_i_init, x2)
		 .then(Bdouble_r_init, x2)
		 .then(Bdouble_i_init, x2)
		 .then(O_r_init, y)
		 .then(O_i_init, y)
		 .then(P_r_init, y)
		 .then(P_i_init, y)
		 .then(Blocal_r_update, computation::root)
		 .then(Blocal_i_update, wnum)
		 .then(Q_r_update, jSprime)
		 .then(Q_i_update, wnum)
		 .then(O_r_update, wnum)
		 .then(O_i_update, wnum)
		 .then(P_r_update, wnum)
		 .then(P_i_update, wnum)
		 .then(Bsingle_r_update, n)
		 .then(Bsingle_i_update, y)
		 .then(Bdouble_r_update0, y)
		 .then(Bdouble_i_update0, y)
		 .then(Bdouble_r_update1, y)
		 .then(Bdouble_i_update1, y);
#endif
    //Q_r_init
    //  .then(Q_i_init, y)
    //  .then(Bsingle_r_init, x2)
    //  .then(Bsingle_i_init, x2)
    //  .then(Q_r_update, jSprime)
    //  .then(Q_i_update, wnum)
    //  .then(Bsingle_r_update, n)
    //  .then(Bsingle_i_update, y);

    computation *handle = &(
        Blocal_r_init
        .then(Blocal_i_init, computation::root)
        .then(Bsingle_r_init, computation::root)
        .then(Bsingle_i_init, computation::root));
    //for (auto computations: Bsingle_updates) {
    //  computation *real;
    //  computation *imag;
    //  std::tie(real, imag) = computations;
    //  handle = handle
    //    .then(*real, y)
    //    .then(*imag, y);
    //}

    var it = computation::root; 
    for (auto edges : blocked_q2userEdges) {
      // remove redundant loads
      for (auto edge : edges) {
        handle = &(handle
            ->then(*edge.q_r, it)
            .then(*edge.q_i, y));
        it = y;
      }
      it = x;
      for (auto edge : edges) {
        handle = &(handle
            ->then(*edge.bl_r, it)
            .then(*edge.bl_i, y));
        it = y;
      }
      it = x;
      for (auto edge : edges) {
        handle = &(handle
            ->then(*edge.bs_r, it)
            .then(*edge.bs_i, y));
        it = y;
      }
      it = x;
    }
    // vectorize
#define VECTOR_WIDTH 4
    for (auto edge : q2userEdges) {
      edge.q_r->vectorize(y, VECTOR_WIDTH);
      edge.q_i->vectorize(y, VECTOR_WIDTH);
      edge.bs_r->vectorize(x2, VECTOR_WIDTH);
      edge.bs_i->vectorize(x2, VECTOR_WIDTH);
      //edge.bl_r->vectorize(x, VECTOR_WIDTH);
      //edge.bl_i->vectorize(x, VECTOR_WIDTH);
    }

    if (PARALLEL)
    {
#if 0
        Blocal_r_init.tag_parallel_level(t);
        Blocal_r_update.tag_parallel_level(t);
#endif
        //Bsingle_r_update.tag_parallel_level(t);
    }

#if 0
    Blocal_r_init.vectorize(x, Vsnk);
    Q_r_init.vectorize(y, Vsrc);

    Blocal_r_update.vectorize(x, Vsnk);
    Q_r_update.vectorize(y, Vsrc);

    Bsingle_r_update.vectorize(x2, Vsnk);
#endif
    //Q_r_init.vectorize(y, Vsrc);
    //Q_r_update.vectorize(y, Vsrc);
    //Bsingle_r_update.vectorize(x2, Vsnk);

    // -------------------------------------------------------
    // Layer III
    // -------------------------------------------------------
    buffer buf_Blocal_r("buf_Blocal_r", {Lt, Nsrc, Nc, Ns, Nc, Ns, Nc, Ns, Vsnk}, p_float64, a_output);
    buffer buf_Blocal_i("buf_Blocal_i", {Lt, Nsrc, Nc, Ns, Nc, Ns, Nc, Ns, Vsnk}, p_float64, a_output);
    buffer buf_Q_r("buf_Q_r", {Lt, Nsrc, Nc, Ns, Nc, Ns, Nc, Ns, Vsnk, Vsnk}, p_float64, a_output);
    buffer buf_Q_i("buf_Q_i", {Lt, Nsrc, Nc, Ns, Nc, Ns, Nc, Ns, Vsnk, Vsnk}, p_float64, a_output);
    buffer buf_O_r("buf_O_r", {Lt, Nsrc, Nc, Ns, Nc, Ns, Nc, Ns, Vsnk, Vsnk}, p_float64, a_output);
    buffer buf_O_i("buf_O_i", {Lt, Nsrc, Nc, Ns, Nc, Ns, Nc, Ns, Vsnk, Vsnk}, p_float64, a_output);
    buffer buf_P_r("buf_P_r", {Lt, Nsrc, Nc, Ns, Nc, Ns, Nc, Ns, Vsnk, Vsnk}, p_float64, a_output);
    buffer buf_P_i("buf_P_i", {Lt, Nsrc, Nc, Ns, Nc, Ns, Nc, Ns, Vsnk, Vsnk}, p_float64, a_output);
    buffer buf_Bsingle_r("buf_Bsingle_r", {Lt, Nsrc, Nc, Ns, Nc, Ns, Nc, Ns, Vsnk, Vsnk}, p_float64, a_output);
    buffer buf_Bsingle_i("buf_Bsingle_i", {Lt, Nsrc, Nc, Ns, Nc, Ns, Nc, Ns, Vsnk, Vsnk}, p_float64, a_output);
    buffer buf_Bdouble_r("buf_Bdouble_r", {Lt, Nsrc, Nc, Ns, Nc, Ns, Nc, Ns, Vsnk, Vsnk}, p_float64, a_output);
    buffer buf_Bdouble_i("buf_Bdouble_i", {Lt, Nsrc, Nc, Ns, Nc, Ns, Nc, Ns, Vsnk, Vsnk}, p_float64, a_output);

    Blocal_r.store_in(&buf_Blocal_r);
    Blocal_i.store_in(&buf_Blocal_i);
    Blocal_r_init.store_in(&buf_Blocal_r);
    Blocal_i_init.store_in(&buf_Blocal_i);
#if 0
    Blocal_r_update.store_in(&buf_Blocal_r, {t, n, iCprime, iSprime, jCprime, jSprime, kCprime, kSprime, x});
    Blocal_i_update.store_in(&buf_Blocal_i, {t, n, iCprime, iSprime, jCprime, jSprime, kCprime, kSprime, x});
#endif

    //Q_r_init.store_in(&buf_Q_r);
    //Q_i_init.store_in(&buf_Q_i);
#if 0
    O_r_init.store_in(&buf_O_r);
    O_i_init.store_in(&buf_O_i);
    P_r_init.store_in(&buf_P_r);
    P_i_init.store_in(&buf_P_i);
#endif

    //Q_r_update.store_in(&buf_Q_r, {t, n, iCprime, iSprime, kCprime, kSprime, jCprime, jSprime, x, y});
    //Q_i_update.store_in(&buf_Q_i, {t, n, iCprime, iSprime, kCprime, kSprime, jCprime, jSprime, x, y});
#if 0
    O_r_update.store_in(&buf_O_r, {t, n, jCprime, jSprime, kCprime, kSprime, iCprime, iSprime, x, y});
    O_i_update.store_in(&buf_O_i, {t, n, jCprime, jSprime, kCprime, kSprime, iCprime, iSprime, x, y});
    P_r_update.store_in(&buf_P_r, {t, n, jCprime, jSprime, kCprime, kSprime, iCprime, iSprime, x, y});
    P_i_update.store_in(&buf_P_i, {t, n, jCprime, jSprime, kCprime, kSprime, iCprime, iSprime, x, y});
#endif
    Bsingle_r_init.store_in(&buf_Bsingle_r);
    Bsingle_i_init.store_in(&buf_Bsingle_i);

    //Bsingle_r_update.store_in(&buf_Bsingle_r, {t, n, iCprime, iSprime, jCprime, jSprime, kCprime, kSprime, x, x2});
    //Bsingle_i_update.store_in(&buf_Bsingle_i, {t, n, iCprime, iSprime, jCprime, jSprime, kCprime, kSprime, x, x2});

#if 0
    Bdouble_r_init.store_in(&buf_Bdouble_r);
    Bdouble_i_init.store_in(&buf_Bdouble_i);
    Bdouble_r_update0.store_in(&buf_Bdouble_r, {t, n, iCprime, iSprime, jCprime, jSprime, kCprime, kSprime, x, x2});
    Bdouble_i_update0.store_in(&buf_Bdouble_i, {t, n, iCprime, iSprime, jCprime, jSprime, kCprime, kSprime, x, x2});
    Bdouble_r_update1.store_in(&buf_Bdouble_r, {t, n, iCprime, iSprime, jCprime, jSprime, kCprime, kSprime, x, x2});
    Bdouble_i_update1.store_in(&buf_Bdouble_i, {t, n, iCprime, iSprime, jCprime, jSprime, kCprime, kSprime, x, x2});
#endif

    for (auto edge : q2userEdges) {
      auto *buf_q_r = new buffer(
          // name
          str_fmt("buf_%s", edge.q_r->get_name().c_str()),
          // dimensions
          //{ Lt, Nc, Ns, Nc, Ns, Vsrc, Vsnk },
          {Vsnk},
          // type
          tiramisu::p_float64, 
          // usage/source
          a_temporary);
      auto *buf_q_i = new buffer(
          // name
          str_fmt("buf_%s", edge.q_i->get_name().c_str()),
          // dimensions
          //{ Lt, Nc, Ns, Nc, Ns, Vsrc, Vsnk },
          {Vsnk},
          // type
          tiramisu::p_float64, 
          // usage/source
          a_temporary);
       edge.q_r->store_in(buf_q_r, {y});
       edge.q_i->store_in(buf_q_i, {y});
    }

    for (auto computations: Bsingle_updates) {
      computation *real;
      computation *imag;
      std::tie(real, imag) = computations;
      real->store_in(&buf_Bsingle_r, {t, n, iCprime, iSprime, jCprime, jSprime, kCprime, kSprime, x, x2});
      imag->store_in(&buf_Bsingle_i, {t, n, iCprime, iSprime, jCprime, jSprime, kCprime, kSprime, x, x2});
    }
    for (auto computations: Blocal_updates) {
      computation *real;
      computation *imag;
      std::tie(real, imag) = computations;
      real->store_in(&buf_Blocal_r, {t, n, iCprime, iSprime, jCprime, jSprime, kCprime, kSprime, x});
      imag->store_in(&buf_Blocal_i, {t, n, iCprime, iSprime, jCprime, jSprime, kCprime, kSprime, x});
    }

    // -------------------------------------------------------
    // Code Generation
    // -------------------------------------------------------
#if 0
    tiramisu::codegen({&buf_Blocal_r, &buf_Blocal_i, prop_r.get_buffer(), prop_i.get_buffer(), weights.get_buffer(), psi_r.get_buffer(), psi_i.get_buffer(), color_weights.get_buffer(), spin_weights.get_buffer(), Bsingle_r_update.get_buffer(), Bsingle_i_update.get_buffer(), Bdouble_r_init.get_buffer(), Bdouble_i_init.get_buffer(), &buf_O_r, &buf_O_i, &buf_P_r, &buf_P_i, &buf_Q_r, &buf_Q_i}, "generated_baryon.o");
#endif
    /* callsite:
	    tiramisu_generated_code(Blocal_r.raw_buffer(),
				    Blocal_i.raw_buffer(),
				    prop_r.raw_buffer(),
				    prop_i.raw_buffer(),
				    weights_t.raw_buffer(),
				    psi_r.raw_buffer(),
				    psi_i.raw_buffer(),
				    color_weights_t.raw_buffer(),
				    spin_weights_t.raw_buffer(),
				    Bsingle_r.raw_buffer(),
				    Bsingle_i.raw_buffer(),
    */
    tiramisu::codegen({&buf_Blocal_r, &buf_Blocal_i, prop_r.get_buffer(), prop_i.get_buffer(), psi_r.get_buffer(), psi_i.get_buffer(), 
        &buf_Bsingle_r, &buf_Bsingle_i, &buf_O_r, &buf_O_i, &buf_P_r, &buf_P_i, &buf_Q_r, &buf_Q_i}, "generated_baryon.o");
}

int main(int argc, char **argv)
{
    generate_function("tiramisu_generated_code");

    return 0;
}
