#include "quant/analytics/StatisticalAnalysis.h"
#include "quant/domain/Errors.h"

#include <algorithm>
#include <cmath>
#include <numeric>
#include <random>
#include <set>

namespace quant::analytics {
namespace {
double mean(const std::vector<double>& x) { return x.empty() ? 0.0 : std::accumulate(x.begin(), x.end(), 0.0) / static_cast<double>(x.size()); }
double sd(const std::vector<double>& x) { if (x.size()<2) return 0.0; double m=mean(x),s=0; for(double v:x)s+=(v-m)*(v-m); return std::sqrt(s/static_cast<double>(x.size()-1)); }
double quantile(std::vector<double> x,double p){ if(x.empty())return 0; std::sort(x.begin(),x.end()); return x[static_cast<std::size_t>(p*static_cast<double>(x.size()-1))]; }
ConfidenceInterval interval(const std::vector<double>& x,double confidence){ double a=(1-confidence)/2; return {mean(x),quantile(x,.5),sd(x),quantile(x,a),quantile(x,1-a)}; }
void validate(const std::vector<DatedReturn>& x,const StatisticalConfig& c){
 if(static_cast<int>(x.size())<c.minimum_observations) throw MethodologyError("Statistical input has insufficient observations");
 if(c.simulations<=0 || c.confidence_level<=0 || c.confidence_level>=1 || c.annualization_factor<=0) throw ConfigurationError("Invalid statistical configuration");
 std::set<std::string>d; for(auto&r:x){if(!std::isfinite(r.value))throw DataError("Non-finite statistical return"); if(!d.insert(r.date).second)throw DataError("Duplicate statistical date: "+r.date);}
}
std::vector<std::size_t> sample_indices(std::size_t n,int block,BootstrapMethod method,std::mt19937& rng){
 std::uniform_int_distribution<std::size_t> pick(0,n-1); std::vector<std::size_t> out; out.reserve(n);
 if(method==BootstrapMethod::Iid){while(out.size()<n)out.push_back(pick(rng));return out;}
 while(out.size()<n){auto start=pick(rng); for(int j=0;j<block && out.size()<n;++j)out.push_back((start+static_cast<std::size_t>(j))%n);} return out;
}
BootstrapMetricRow metrics(int id,const std::vector<double>& r,const std::vector<double>& active,double ann){
 double wealth=1,peak=1,mdd=0; std::vector<double> down; for(double v:r){wealth*=1+v;peak=std::max(peak,wealth);mdd=std::min(mdd,wealth/peak-1);if(v<0)down.push_back(v);}
 double vol=sd(r)*std::sqrt(ann), years=static_cast<double>(r.size())/ann, ar=years>0?std::pow(wealth,1/years)-1:0;
 double sh=vol>0?mean(r)*ann/vol:0, dd=sd(down)*std::sqrt(ann), so=dd>0?mean(r)*ann/dd:0;
 double aw=1;for(double v:active)aw*=1+v; double ir=sd(active)>0?mean(active)*std::sqrt(ann)/sd(active):0;
 return {id,wealth-1,ar,vol,sh,so,mdd,mdd<0?ar/std::abs(mdd):0,wealth,aw-1,ir};
}
}
std::string to_string(BootstrapMethod m){return m==BootstrapMethod::Iid?"iid_comparison":"moving_block_circular";}
int StatisticalAnalyzer::suggested_block_length(std::size_t n){return std::max(1,static_cast<int>(std::round(std::cbrt(static_cast<double>(n)))));}
StatisticalResult StatisticalAnalyzer::bootstrap(const std::string& id,const std::string& input,const std::vector<DatedReturn>& returns,
 const std::vector<DatedReturn>& benchmark,const std::string& benchmark_name,const StatisticalConfig& config,bool diagnostic){
 validate(returns,config); if(!benchmark.empty()){validate(benchmark,config);if(benchmark.size()!=returns.size())throw MethodologyError("Strategy and benchmark lengths differ");for(std::size_t i=0;i<returns.size();++i)if(returns[i].date!=benchmark[i].date)throw MethodologyError("Strategy and benchmark dates differ");}
 if(input.find("normalized_window")!=std::string::npos)throw MethodologyError("Normalized-window OOS cannot be used as deployable history");
 if(input.find("full_sample")!=std::string::npos && !diagnostic)throw MethodologyError("Full-sample input requires explicit diagnostic labelling");
 StatisticalResult out; out.experiment_id=id;out.input_series=input;out.start_date=returns.front().date;out.end_date=returns.back().date;out.benchmark=benchmark_name;
 out.method=to_string(config.method);out.seed=config.seed;out.simulations=config.simulations;out.block_length=config.block_length>0?config.block_length:suggested_block_length(returns.size());
 out.confidence_level=config.confidence_level;out.observation_count=static_cast<int>(returns.size());out.annualization_method="source_result_factor="+std::to_string(config.annualization_factor);
 out.assumptions="empirical returns; circular moving blocks preserve within-block chronology; heuristic block length is not optimal";out.input_returns=returns;out.benchmark_returns=benchmark;
 if(config.minimum_observations<30)out.warnings.push_back("small deterministic fixture; not suitable for research inference");
 if(out.block_length<=0 || out.block_length>out.observation_count)throw ConfigurationError("Invalid bootstrap block length");
 std::mt19937 rng(config.seed);std::vector<double> cum,sharp;int loss=0,pos=0,under=0,shpos=0,shexceed=0;
 for(int s=0;s<config.simulations;++s){auto idx=sample_indices(returns.size(),out.block_length,config.method,rng);std::vector<double> r,a,b;for(auto i:idx){r.push_back(returns[i].value);double bv=benchmark.empty()?0:benchmark[i].value;b.push_back(bv);a.push_back(returns[i].value-bv);}auto m=metrics(s,r,a,config.annualization_factor);auto bm=metrics(s,b,b,config.annualization_factor);out.metrics.push_back(m);if(s<20)out.sampled_paths.push_back(r);cum.push_back(m.cumulative_return);sharp.push_back(m.sharpe);loss+=m.cumulative_return<0;pos+=m.active_return>0;under+=m.active_return<0;shpos+=m.sharpe>0;shexceed+=m.sharpe>bm.sharpe;}
 out.cumulative_return_ci=interval(cum,config.confidence_level);out.sharpe_ci=interval(sharp,config.confidence_level);double n=config.simulations;
 out.probability_loss=loss/n;out.probability_positive_active=pos/n;out.probability_benchmark_underperformance=under/n;out.probability_sharpe_positive=shpos/n;out.probability_sharpe_exceeds_benchmark=shexceed/n;return out;
}
MultipleTestingResult StatisticalAnalyzer::reality_check(const std::vector<std::vector<double>>& c,const StatisticalConfig& config){
 if(c.empty())throw MethodologyError("Reality check requires candidates");std::size_t n=c.front().size();if(static_cast<int>(n)<config.minimum_observations)throw MethodologyError("Reality check sample too small");for(auto&x:c)if(x.size()!=n)throw MethodologyError("Candidate return lengths differ");
 double observed=-1e300;for(auto&x:c)observed=std::max(observed,mean(x));int block=config.block_length>0?config.block_length:suggested_block_length(n);if(block<=0||block>static_cast<int>(n))throw ConfigurationError("Invalid reality-check block length");
 std::mt19937 rng(config.seed);int exceed=0;for(int s=0;s<config.simulations;++s){auto idx=sample_indices(n,block,BootstrapMethod::MovingBlock,rng);double best=-1e300;for(auto&x:c){double m=mean(x),v=0;for(auto i:idx)v+=x[i]-m;best=std::max(best,v/static_cast<double>(n));}exceed+=best>=observed;}
 return {"centered_moving_block_reality_check",static_cast<int>(c.size()),static_cast<int>(c.size()),observed,(1.0+exceed)/(config.simulations+1.0),config.seed,config.simulations,block};
}
} // namespace quant::analytics
