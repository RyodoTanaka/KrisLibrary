#include "LP_InteriorPoint.h"
#include <math/linalgebra.h>
#include <math/Conditioner.h>
#include <math/SVDecomposition.h>
#include <math/VectorPrinter.h>
#include <math/MatrixPrinter.h>
#include <errors.h>
#include <iostream>
using namespace Optimization;
using namespace std;
using namespace Math;

#define kMaxIters_Outer   30
#define kMaxIters_Inner   10

struct HConditioner : public Conditioner_SymmDiag
{
  HConditioner(Matrix& A,Vector& b)
    :Conditioner_SymmDiag(A,b,NormalizeDiagonal)
    //:Conditioner_SymmDiag(A,b,None)
  {}
};

void ConditionMatrix(Matrix& A,Real relZeroTolerance=1e-8)
{
  Real relZero=A.maxAbsElement()*relZeroTolerance;
  for(int i=0;i<A.m;i++)
    for(int j=0;j<A.n;j++)
      if(Abs(A(i,j)) <= relZero) A(i,j)=Zero;
}

LP_InteriorPointSolver::LP_InteriorPointSolver()
  :tol_zero(1e-5),tol_inner(1e-7),tol_outer(1e-5), objectiveBreak(-dInf)
{}

void LP_InteriorPointSolver::SetInitialPoint(const Vector& _x0)
{
  x0 = _x0;
  if(x0.n != A.n) {
    cout<<"ERROR: initial point has incorrect dimensions"<<endl; 
    x0.resize(0);
  }
}

// Returns the value of the objective function, including the inequality barrier functions
double LP_InteriorPointSolver::Objective_Ineq(const Vector &x, double t) {
  double obj = t*dot(c,x);
  for (int i=0; i<A.m; i++) {
    Real vali = p(i) - A.dotRow(i,x);
    obj -= Log(vali);
  }
  return obj;
}

bool LP_InteriorPointSolver::FindFeasiblePoint()
{
	if (verbose>=1) cout << " - finding feasible point:" << endl;
	if (verbose>=2) cout << " - setup feasibility problem" << endl;

	LP_InteriorPointSolver lp;
	lp.Resize(A.m,A.n+1);
	lp.c.setZero();  lp.c(0)=1.0;

	for(int i=0;i<A.m;i++) lp.A(i,0)=-1.0;
	lp.A.copySubMatrix(0,1,A);
	lp.p = p;
	
	Vector x0_feas(c.n+1,Zero);
	x0_feas(0)=1e6;
	lp.SetInitialPoint(x0_feas);
	
	if (verbose>=2) cout << " - create feasibility problem LP" << endl;
	lp.SetObjectiveBreak(0);
	lp.verbose = verbose;
	
	if (verbose>=2) cout << " - solve feasibility problem LP" << endl;
	bool res = (lp.Solve()!=Infeasible);
	
	if (!res) {
		if (verbose>=1) {
			cout << " - found NO feasible point" << endl;
		}
		return false;
	} else {
		if (verbose>=1) {
			cout << " - found a feasible point" << endl;
		}
		if (x0.n != 0) cout << "ERROR: x0 is supposed to be NULL in LP_InteriorPointSolver::findFeasiblePoint()" << endl;
		const Vector& xopt_feas = lp.GetOptimum();
		x0.resize(c.n);
		xopt_feas.getSubVectorCopy(1,x0);
		if(!SatisfiesInequalities(x0)) {
		  cerr<<"ERROR: initial point solved by LP_InteriorPointSolver::FindFeasibleInitialPoint() is not feasible!"<<endl;
		  return false;
		}
		return true;
	}
}

LP_InteriorPointSolver::Result LP_InteriorPointSolver::Solve()
{
  if (verbose>=1) cout << "Solving LP_InteriorPoint:" << endl;
  
  if (x0.n == 0) {
    if (! FindFeasiblePoint()) {
      if(verbose>=1) cout << "Couldn't find an initial feasible point in LP_InteriorPointSolver::solve()" << endl;
      return Infeasible;
    }
  }
	
  if (verbose>=2) { cout << "x0 = "<<VectorPrinter(x0)<<endl; }
	
  // Make sure starting xopt is always set to x0 when we begin to solve.
  xopt = x0;
    
  // Already checked that the initial point is feasible.
	
  //
  // Algorithm parameters
  //
  
  //	t	- indicates how far the search is along the "central path"; basically,
  //		  it just trades off how much we weight the goal vs. the inequality barriers.
  double t=0.01;
  //	mu	- multiple by which we increase t at each outer iteration.
  double mu=15;
  //	alpha	- ? - in backtracking line search, I think this indicates a goal decrement or something.
  //	beta	- in backtracking line search, how much we back off at each iteration
  double alpha=0.2;
  double beta=0.5;
	
  //
  // Outer Loop
  //

  DiagonalMatrix d;
  Matrix Hsub,H;
  Vector g;
  Vector dx;
  Vector xcur;
  //cout<<"A is "<<endl<<MatrixPrinter(A)<<endl;
  //cout<<"b is "<<VectorPrinter(b)<<endl;
  //getchar();
	
  double gap=1.0;
  int iter_outer=0;
  while (gap>tol_outer) {
    if(verbose >= 2) { cout << "Current xopt = "<<VectorPrinter(xopt)<<endl; }
		
    ++iter_outer;
    if (iter_outer > kMaxIters_Outer) {
      cout << "WARNING: LP_InteriorPoint outer loop did not converge within max iters" << endl;
      return MaxItersReached;
    }
		
    // Update position along the central path
    t *= mu;
		
    if (verbose>=2) cout << " Outer iter " << iter_outer << ", t=" << t << endl;
		
    //
    // Inner Loop
    //
		
    double dec=1.0;
    int iter_inner=0;
    while ((fabs(dec)>tol_inner) && (iter_inner <= kMaxIters_Inner)) {
      ++iter_inner;
      /*
	if (iter_inner > kMaxIters_Inner) {
	cout << "WARNING: LP_InteriorPoint inner loop did not converge within max iters" << endl;
	return MaxItersReached;
	}
      */
			
      if (verbose>=3) cout << "  Inner iter " << iter_inner << ", dec=" << dec << endl;
			
      // (1) Compute the direction to descend
      //		- Newton direction is dx = -(Hessian^-1)*(gradient)
      //		- H = (Aineq'*(diag(d.^2))*Aineq)
      //		- g = (t*f) + (Aineq'*d)
			
      //cout << "Size of Aineq is " << A.m << " x " << A.n << endl;

      //d = (bineq-Aineq*xopt)^-1 component-wise
      A.mul(xopt,d);  d.inplaceNegative();  d += p;
      d.inplacePseudoInverse();
      //Hsub = diag(d)*Aineq
      d.preMultiply(A,Hsub);
      //H = Hsub'*Hsub
      H.mulTransposeA(Hsub,Hsub);		

      //g = Aineq'*d+t*c	
      A.mulTranspose(d,g);
      g.madd(c,t);
      Matrix Htemp=H;
      Vector gtemp=g;
      // Solve the system to find Newton direction
      // Try using a better numerically conditioned matrix
      HConditioner Hinv(Htemp,gtemp);
      //if(!Hinv.Solve_SVD(dx)) {
      if(!Hinv.Solve_Cholesky(dx)) {
	RobustSVD<Real> svd;
	if(svd.set(Hinv.A)) {
	  svd.backSub(Hinv.b,dx);
	  if(verbose >= 1) cout<<"Solved by SVD"<<endl;
	  if(verbose >= 2) {
	    cout<<"Singular values "<<svd.svd.W<<endl;
	    cout<<"b "<<Hinv.b<<endl;
	    getchar();
	  }
	}
	else {
	  if(verbose >= 1) {
	    //cout<<"H Matrix "<<endl; H.print();
	    //cout<<"Scale is "; Hinv.S.print();
	    cout<<"Scaled H Matrix "<<endl<<MatrixPrinter(Hinv.A)<<endl;
	    cout<<"LP: Error performing H^-1*g!"<<endl;
	  }
	  return Error;
	}
      }
      Hinv.Post(dx);
      //getchar();

      dx.inplaceNegative();

      /*			
      // TEMPORARY!!!
      cout << "H" << endl; H.print();
      cout << "d" << endl; d.print();
      cout << "g" << endl; g.print();
      cout << "dx" << endl; dx.print();
      */
			
      // (2) Compute decrement (a scalar) -> dec=abs(-g'*dx)
      dec = dot(g,dx);

      // (3) Line search along dx, using backtracking
      double s=1.0;
      Real obji_xopt = Objective_Ineq(xopt,t);
      //cout<<"Starting search at "<< Objective(xopt)<<", "; xopt.print();
      //cout<<"   direction "; dx.print();
      //can find max on s outside of the loop
      {
	Real smax = 2;
	for(int i=0;i<A.m;i++) {
	  Real r = p(i) - A.dotRow(i,xopt);
	  Real q = A.dotRow(i,dx);
	  Assert(r >= Zero);
	  if(q > Zero)
	    smax = Min(smax,r/q);
	}
	Assert(smax >= 0.0);
	if(smax <= 1.0)
	  s = smax*0.99;
      }

      int iter_backtrack=0;
      bool done_backtrack=false;
      while (! done_backtrack) {
	++iter_backtrack;
	if (verbose>=4) cout << "   Backtrack iter " << iter_backtrack << ", s=" << s << ", dec=" << dec << endl;
	xcur = xopt; xcur.madd(dx,s);
	bool sat_ineq=SatisfiesInequalities(xcur);
	//inequalities will be satisfied unless there's some numerical error
	/*
	if(!sat_ineq) {
	  Real maxViolation=0;
	  for(int i=0;i<A.m;i++) {
	    Real r = b(i) - A.dotRow(i,xcur);
	    maxViolation = Max(maxViolation,-r);
	  }
	  if(!FuzzyZero(maxViolation)) {
	    cout<<"WHAT?!?! s is "<<s<<" on iter "<<iter_backtrack<<endl;
	    cout<<"Maximum violation is "<<maxViolation<<endl;  
	    cout<<"xcur "; xcur.print();
	    cout<<"dx "; dx.print();
	    getchar();
	  }
	}
	*/
	if (sat_ineq && (Objective_Ineq(xcur,t)<=(obji_xopt+(alpha*s*dec)))) {
	  xopt=xcur;
	  done_backtrack = true;
	} else {
	  s *= beta;
	  if(s < 1e-10) {
	    if(verbose >= 2) cout<<"s is really small, breaking"<<endl;
	    done_backtrack = true;
	  }
	}
      }

      if (verbose>=3) cout << "  Inner iter " << iter_inner << ", obj=" << Objective(xopt) << ", s=" << s << ", dec=" << dec << ", gap=" << ((double) p.n) / t << endl;
      //if (verbose>=4) cout << "   Backtrack iter " << iter_backtrack << ", s=" << s << ", dec=" << dec << endl;
			
      /*
	while ((f_objective(x+(s*dx),f,Aineq,bineq,t) > (f_objective(x,f,Aineq,bineq,t)+(alpha*s*g'*dx))) | (max((Aineq*(x+(s*dx)))-bineq) > 0))
	iter_bt = iter_bt + 1;
	if (diagnostics) disp(['   Backtrack iter ' num2str(iter_bt)]); end
	s = beta*s;
	%if (max((A*(x+(s*dx)))>b) > 0)
	%    disp('Error: current iteration of x is not feasible when backtracking.');
	%    return;
	%end
	end
	% (4) Update x with the value obtained from backtracking.
	x = x + (s*dx);
      */
			
      if (!IsInf(objectiveBreak) && (Objective(xopt) < objectiveBreak)) return SubOptimal;
      if(s < 1e-10) break;
    }
    
    gap = ((double) p.n) / t;
  }
   
  if (verbose>=1) cout << "Optimization was successful." << endl;

  // If we were going to stop when the objective is negative, check that the
  // objective actually _became_ negative!
  if (!IsInf(objectiveBreak) && (Objective(xopt) >= objectiveBreak)) {
    if(verbose>=1) cout<<"The max objective is not achieved! "<<Objective(xopt)<<endl;
    return OptimalNoBreak;
  }
  else return Optimal;
}


LP_InteriorPoint::LP_InteriorPoint()
{}

bool LP_InteriorPoint::Set(const LinearProgram& lp)
{
  Matrix Aeq;
  Vector beq;
  int neq=0,nineq=0;
  for(int i=0;i<lp.A.m;i++) {
    if(lp.ConstraintType(i) == LinearProgram::Fixed) neq++;
    else {
      if(lp.HasLowerBound(lp.ConstraintType(i))) nineq++;
      if(lp.HasUpperBound(lp.ConstraintType(i))) nineq++;
    }
  }
  for(int i=0;i<lp.A.n;i++) {
    if(lp.VariableType(i) == LinearProgram::Fixed) neq++;
    else {
      if(lp.HasLowerBound(lp.VariableType(i))) nineq++;
      if(lp.HasUpperBound(lp.VariableType(i))) nineq++;
    }
  }

  if(neq == 0) {
    x0.clear();
    N.clear();
    ((LinearProgram&)solver) = lp;
    //solver.minimize is ignored by the solver
    if(!solver.minimize) solver.c.inplaceNegative();
    return true;
  }

  Aeq.resize(neq,lp.A.n);
  beq.resize(neq);
  neq=0;
  for(int i=0;i<lp.A.m;i++) {
    if(lp.ConstraintType(i)==LinearProgram::Fixed)
    {
      Vector Ai;
      lp.A.getRowRef(i,Ai);
      Aeq.copyRow(neq,Ai);
      beq(neq) = lp.p(i);
      neq++;
    }
  }
  for (int i=0;i<lp.A.n;i++) {
    if(lp.VariableType(i)==LinearProgram::Fixed)
    {
      Vector Aeqi;
      Aeq.getRowRef(i,Aeqi);
      Aeqi.setZero();
      Aeqi(i) = One;
      beq(neq) = lp.l(i);
      neq++;
    }
  }

  SVDecomposition<Real> svd;
  if(!svd.set(Aeq)) {
    if(solver.verbose>=1) cout<<"LP_InteriorPoint: Couldn't set SVD of equality constraints!!!"<<endl;
    return false;
  }
  svd.backSub(beq,x0);
  svd.getNullspace(N);

  //Set the solver to use the new variable y
  if(N.n == 0) {  //overconstrained!
    cout<<"Overconstrained!"<<endl;
    solver.Resize(0,0);
    return true;
  }

  if(nineq == 0) {
    cout<<"No inequalities!"<<endl;
    abort();
    return true;
  }

  if(solver.verbose >= 1) cout<<"LP_InteriorPoint: Decomposed the problem from "<<lp.A.n<<" to "<<N.n<<" variables"<<endl;

  solver.Resize(nineq,N.n);
  //objective
  foffset = dot(lp.c,x0);
  //c is such that c'*y = lp.c'*N*y => c = N'*lp.c
  N.mulTranspose(lp.c,solver.c);
  solver.minimize = lp.minimize;
  if(!solver.minimize) solver.c.inplaceNegative();


  //inequality constraints
  //q <= Aineq*x <= p
  //q <= Aineq*x0 + Aineq*N*y <= p
  //q - Aineq*x0 <= Aineq*N*y <= p-Aineq*x0
  //==> -Aineq*N*y <= -q + Aineq*x0
  nineq=0;
  for(int i=0;i<lp.A.m;i++) {
    if(lp.ConstraintType(i)==LinearProgram::Fixed) continue;
    if(lp.HasUpperBound(lp.ConstraintType(i))) {
      Vector Ai,sAi;
      lp.A.getRowRef(i,Ai);
      solver.A.getRowRef(nineq,sAi);
      N.mulTranspose(Ai,sAi);
      solver.p(nineq) = lp.p(i) - dot(Ai,x0);
      nineq++;
    }
    if(lp.HasLowerBound(lp.ConstraintType(i))) {
      Vector Ai,sAi;
      lp.A.getRowRef(i,Ai);
      solver.A.getRowRef(nineq,sAi);
      N.mulTranspose(Ai,sAi);
      sAi.inplaceNegative();
      solver.p(nineq) = dot(Ai,x0) - lp.q(i);
      nineq++;
    }
  }

  //transform bounds to inequality constraints
  for(int i=0;i<lp.u.n;i++) {
    if(lp.VariableType(i)==LinearProgram::Fixed) continue;
    if(lp.HasLowerBound(lp.VariableType(i))) {
      //-xi < -li
      //-ei'*N*y <= -li+ei'*x0
      Vector Ni,sAi;
      N.getRowRef(i,Ni);
      solver.A.getRowRef(nineq,sAi);
      sAi.setNegative(Ni);
      solver.p(nineq) = -lp.l(i) + x0(i);
      nineq++;
    }
    if(lp.HasUpperBound(lp.VariableType(i))) {
      //xi < ui
      //ei'*N*y <= ui-ei'*x0
      Vector Ni,sAi;
      N.getRowRef(i,Ni);
      solver.A.getRowRef(nineq,sAi);
      sAi.copy(Ni);
      solver.p(nineq) = lp.u(i) - x0(i);
      nineq++;
    }
  }
  Assert(solver.IsValid());
  return true;
}

LP_InteriorPointSolver::Result LP_InteriorPoint::Solve()
{
  return solver.Solve();
}

void LP_InteriorPoint::SetObjective(const Vector& c)
{
  Assert(c.n == x0.n);
  foffset = dot(c,x0);
  N.mulTranspose(c,solver.c);
  if(!solver.minimize) solver.c.inplaceNegative();
}

void LP_InteriorPoint::SetInitialPoint(const Vector& xinit)
{
  cout<<"TODO: SetInitialPoint"<<endl;
  abort();
  //solve for xinit = x0 + N*yinit
  Vector dx; dx.sub(xinit,x0);
  //solve for dx = N*yinit
  Vector yinit;
  solver.SetInitialPoint(yinit);
}

void LP_InteriorPoint::GetInitialPoint(Vector& xinit) const
{
  N.mul(solver.GetX0(),xinit);
  xinit += x0;
}

void LP_InteriorPoint::GetOptimum(Vector& xopt) const
{
  N.mul(solver.GetOptimum(),xopt);
  xopt += x0;
}

void LP_InteriorPoint::SetObjectiveBreak(double val)
{
  if(solver.minimize) {
    //min f'*x = min c'*y + foffset
    //to get f'*x < val, then c'*y < val-foffset
    solver.SetObjectiveBreak(val-foffset);
  }
  else {
    //max f'*x = -min c'*y + foffset
    //to get f'*x > val, then -c'*y > val - foffset => c'*y < foffset-val
    solver.SetObjectiveBreak(foffset-val);
  }
}
