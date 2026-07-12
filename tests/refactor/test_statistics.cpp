#include "quant/analytics/StatisticalAnalysis.h"
#include "quant/domain/Errors.h"
#include <cmath>
#include <iostream>
#include <stdexcept>

int main(){int cases=0;auto ck=[&](bool x,const char*n){++cases;if(!x)throw std::runtime_error(n);};
 try{std::vector<quant::analytics::DatedReturn> r,b;for(int i=0;i<60;++i){char d[16];std::snprintf(d,sizeof(d),"2024-01-%02d",i+1);r.push_back({d,0.001*(i%7-2)});b.push_back({d,0.0002});}
 quant::analytics::StatisticalConfig c;c.seed=17;c.simulations=100;c.block_length=5;c.minimum_observations=30;c.annualization_factor=365.25;
 auto a=quant::analytics::StatisticalAnalyzer::bootstrap("x","continuous_oos_returns",r,b,"B",c);auto z=quant::analytics::StatisticalAnalyzer::bootstrap("x","continuous_oos_returns",r,b,"B",c);
 ck(a.metrics[0].sharpe==z.metrics[0].sharpe,"moving reproducibility");ck(a.sampled_paths[0].size()==r.size(),"path length");ck(a.block_length==5,"block explicit");ck(a.cumulative_return_ci.lower<=a.cumulative_return_ci.upper,"ci order");ck(a.probability_loss>=0&&a.probability_loss<=1,"probability bounds");ck(a.observation_count==60,"observation metadata");ck(a.seed==17,"seed metadata");ck(a.method=="moving_block_circular","method metadata");
 c.method=quant::analytics::BootstrapMethod::Iid;auto iid=quant::analytics::StatisticalAnalyzer::bootstrap("x","continuous_oos_returns",r,b,"B",c);auto iid2=quant::analytics::StatisticalAnalyzer::bootstrap("x","continuous_oos_returns",r,b,"B",c);ck(iid.metrics[2].sharpe==iid2.metrics[2].sharpe,"iid reproducibility");ck(iid.method=="iid_comparison","iid labelled");
 auto positive=r;for(auto&x:positive)x.value=.01;auto p=quant::analytics::StatisticalAnalyzer::bootstrap("p","continuous_oos_returns",positive,b,"B",c);ck(p.probability_loss==0,"positive series");ck(p.probability_sharpe_positive==1,"positive sharpe");
 auto negative=r;for(auto&x:negative)x.value=-.01;auto n=quant::analytics::StatisticalAnalyzer::bootstrap("n","continuous_oos_returns",negative,b,"B",c);ck(n.probability_loss==1,"negative series");
 auto same=quant::analytics::StatisticalAnalyzer::bootstrap("s","continuous_oos_active_returns",b,b,"B",c);ck(same.probability_positive_active==0,"identical benchmark");ck(std::abs(same.metrics[0].active_return)<1e-12,"active return");
 ck(quant::analytics::StatisticalAnalyzer::suggested_block_length(100)>1,"block heuristic");
 bool bad=false;c.block_length=1000;try{(void)quant::analytics::StatisticalAnalyzer::bootstrap("x","continuous_oos_returns",r,b,"B",c);}catch(const quant::ConfigurationError&){bad=true;}ck(bad,"invalid block");
 bad=false;c.block_length=5;auto short_r=std::vector<quant::analytics::DatedReturn>(r.begin(),r.begin()+10);try{(void)quant::analytics::StatisticalAnalyzer::bootstrap("x","continuous_oos_returns",short_r,{},"",c);}catch(const quant::MethodologyError&){bad=true;}ck(bad,"insufficient sample");
 bad=false;try{(void)quant::analytics::StatisticalAnalyzer::bootstrap("x","normalized_window_oos",r,b,"B",c);}catch(const quant::MethodologyError&){bad=true;}ck(bad,"normalized rejected");
 bad=false;try{(void)quant::analytics::StatisticalAnalyzer::bootstrap("x","full_sample_returns",r,b,"B",c);}catch(const quant::MethodologyError&){bad=true;}ck(bad,"full sample label");
 auto diag=quant::analytics::StatisticalAnalyzer::bootstrap("x","full_sample_returns",r,b,"B",c,true);ck(diag.observation_count==60,"diagnostic allowed");
 std::vector<std::vector<double>> candidates(3,std::vector<double>(60));for(std::size_t i=0;i<60;++i){candidates[0][i]=.001;candidates[1][i]=-.001;candidates[2][i]=0;}
 auto rc=quant::analytics::StatisticalAnalyzer::reality_check(candidates,c);ck(rc.candidate_count==3,"candidate count");ck(rc.p_value>=0&&rc.p_value<=1,"reality p value");ck(rc.observed_best_mean>0,"best mean");
 ck(std::isfinite(a.sharpe_ci.mean),"sharpe inference");ck(a.probability_sharpe_exceeds_benchmark>=0,"sharpe benchmark");ck(a.sampled_paths.size()==20,"sample export");
 std::cout<<cases<<" statistical cases passed\n";return 0;}catch(const std::exception&e){std::cerr<<e.what()<<'\n';return 1;}}
