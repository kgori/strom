#pragma once

#include <iostream>
#include "chain.hpp"
#include "tree_summary.hpp"
#include "likelihood.hpp"
#include "output_manager.hpp"
#include <boost/program_options.hpp>

namespace strom {

class Strom
    {
    public:
                                    Strom();
                                    ~Strom();

        void                        processCommandLineOptions(int argc, const char * argv[]);
        void                        run();

    private:
        void                        clear();//POLWARN
        void                        sample(unsigned iter, Chain::SharedPtr chain, TreeManip::SharedPtr tm, GTRModel::SharedPtr gtr);

        //POLNEW added code
        void                        calcHeatingPowers();
        void                        initChains(OutputManager::SharedPtr outmgr, std::string & newick);
        void                        stopTuningChains();
        void                        stepChains(unsigned iteration, bool sampling);
        void                        swapChains();
        void                        stopChains();
        void                        swapSummary() const;
        void                        showLambdas() const;

        OutputManager::SharedPtr    _output_manager;

        std::string                 _data_file_name;
        std::string                 _tree_file_name;

        double                      _expected_log_likelihood;
        double                      _gamma_shape;
        unsigned                    _num_categ;
        std::vector<double>         _state_frequencies;
        std::vector<double>         _exchangeabilities;

        unsigned                    _random_seed;
        unsigned                    _num_iter;
        unsigned                    _sample_freq;

        static std::string          _program_name;
        static unsigned             _major_version;
        static unsigned             _minor_version;

        //POLNEW added code
        unsigned                    _num_burnin_iter;
        unsigned                    _num_chains;
        double                      _heating_lambda;
        GTRModel::SharedPtr         _gtr;
        Likelihood::SharedPtr       _likelihood;
        Lot::SharedPtr              _lot;
        std::vector<Chain>          _chains;
        std::vector<double>         _heating_powers;
        std::vector<unsigned>       _swaps;
    };

inline Strom::Strom()
    {
    //std::cout << "Constructing a Strom" << std::endl;
    clear();//POLWARN
    }

inline Strom::~Strom()
    {
    //std::cout << "Destroying a Strom" << std::endl;
    }

inline void Strom::clear()
    {
    _output_manager = nullptr;
    _gtr            = nullptr;
    _likelihood      = nullptr;
    _lot            = nullptr;

    _state_frequencies.resize(0);
    _exchangeabilities.resize(0);
    _chains.resize(0);
    _heating_powers.resize(0);
    _swaps.resize(0);

    _data_file_name = "";
    _tree_file_name = "";

    _expected_log_likelihood = 0.0;
    _gamma_shape             = 0.5;
    _heating_lambda          = 0.5;

    _num_categ       = 1;
    _random_seed     = 1;
    _sample_freq     = 1;
    _num_chains      = 1;
    _num_iter        = 1000;
    _num_burnin_iter = 1000;
    }

//POLWARN
inline void Strom::processCommandLineOptions(int argc, const char * argv[])
    {
    boost::program_options::variables_map       vm;
    boost::program_options::options_description desc("Allowed options");
    desc.add_options()
        ("help,h",       "produce help message")
        ("version,v",    "show program version")
        ("seed,z",       boost::program_options::value(&_random_seed)->default_value(1), "pseudorandom number seed")
        ("niter,n",      boost::program_options::value(&_num_iter)->default_value(1000), "number of MCMC iterations")
        ("samplefreq",   boost::program_options::value(&_sample_freq)->default_value(1), "skip this many iterations before sampling next")
        ("datafile,d",   boost::program_options::value(&_data_file_name)->required(), "name of data file in NEXUS format")
        ("treefile,t",   boost::program_options::value(&_tree_file_name)->required(), "name of data file in NEXUS format")
        ("expectedLnL",  boost::program_options::value(&_expected_log_likelihood)->default_value(0.0), "log likelihood expected")
        ("gammashape,s", boost::program_options::value(&_gamma_shape)->default_value(0.5), "shape parameter of the Gamma among-site rate heterogeneity model")
        ("ncateg,c",     boost::program_options::value(&_num_categ)->default_value(1), "number of categories in the discrete Gamma rate heterogeneity model")
        ("statefreq,f",  boost::program_options::value(&_state_frequencies)->multitoken()->default_value(std::vector<double> {0.25, 0.25, 0.25, 0.25}, "0.25 0.25 0.25 0.25"),  "state frequencies in the order A C G T (will be normalized to sum to 1)")
        ("rmatrix,r",    boost::program_options::value(&_exchangeabilities)->multitoken()->default_value(std::vector<double> {1, 1, 1, 1, 1, 1}, "1 1 1 1 1 1"),                "GTR exchangeabilities in the order AC AG AT CG CT GT (will be normalized to sum to 1)")
        //POLNEW
        ("nchains",      boost::program_options::value(&_num_chains)->default_value(1), "number of chains")
        ("heatfactor",   boost::program_options::value(&_heating_lambda)->default_value(0.5), "determines how hot the heated chains are")
        ("burnin",       boost::program_options::value(&_num_burnin_iter)->default_value(100), "number of iterations used to burn in chains")
        ;
    boost::program_options::store(boost::program_options::parse_command_line(argc, argv, desc), vm);
    boost::program_options::notify(vm);

    // If user specified --help on command line, output usage summary and quit
    if (vm.count("help") > 0)
        {
        std::cout << desc << "\n";
        std::exit(1);
        }

    // If user specified --version on command line, output version and quit
    if (vm.count("version") > 0)
        {
        std::cout << boost::str(boost::format("This is %s version %d.%d") % _program_name % _major_version % _minor_version) << std::endl;
        std::exit(1);
        }

    // Be sure state frequencies sum to 1.0 and are all positive
    double sum_freqs = std::accumulate(_state_frequencies.begin(), _state_frequencies.end(), 0.0);
    for (auto & freq : _state_frequencies)
        {
        if (freq <= 0.0)
            throw XStrom("all statefreq entries must be positive real numbers");
        freq /= sum_freqs;
        }

    // Be sure exchangeabilities sum to 1.0 and are all positive
    double sum_xchg = std::accumulate(_exchangeabilities.begin(), _exchangeabilities.end(), 0.0);
    for (auto & xchg : _exchangeabilities)
        {
        if (xchg <= 0.0)
            throw XStrom("all rmatrix entries must be positive real numbers");
        xchg /= sum_xchg;
        }

    // Be sure gamma shape parameter is positive
    if (_gamma_shape <= 0.0)
        throw XStrom("gamma shape must be a positive real number");

    // Be sure number of gamma rate categories is greater than or equal to 1
    if (_num_categ < 1)
        throw XStrom("ncateg must be a positive integer greater than 0");

    //POLNEW
    // Be sure number of chains is greater than or equal to 1
    if (_num_chains < 1)
        throw XStrom("nchains must be a positive integer greater than 0");

    //POLNEW
    // Be sure heatfactor is between 0 and 1
    if (_heating_lambda <= 0.0 || _heating_lambda > 1.0)
        throw XStrom("heatfactor must be a real number in the interval (0.0,1.0]");
    }

inline void Strom::sample(unsigned iteration, Chain::SharedPtr chain, TreeManip::SharedPtr tm, GTRModel::SharedPtr gtr)
    {
    if (iteration % _sample_freq == 0)
        {
        double logLike = chain->calcLogLikelihood();
        double logPrior = chain->calcLogJointPrior();
        double TL = tm->calcTreeLength();
        _output_manager->outputConsole(boost::str(boost::format("%12d %12.5f %12.5f") % iteration % logLike % logPrior));
        _output_manager->outputTree(iteration, tm);
        _output_manager->outputParameters(iteration, logLike, logPrior, TL, gtr);
        }
    }

inline void Strom::calcHeatingPowers()
    {
    // Specify chain heating power (e.g. _heating_lambda = 0.2)
    // chain_index  power
    //      0       1.000 = 1/(1 + 0.2*0)
    //      1       0.833 = 1/(1 + 0.2*1)
    //      2       0.714 = 1/(1 + 0.2*2)
    //      3       0.625 = 1/(1 + 0.2*3)
    unsigned i = 0;
    for (auto & h : _heating_powers)
        {
        h = 1.0/(1.0 + _heating_lambda*i++);
        }
    }

inline void Strom::initChains(OutputManager::SharedPtr outmgr, std::string & newick)
    {
    int chain_index = 0;
    for (auto & c : _chains)
        {
        c.setOutputManager(outmgr);

        // Give the chain a starting tree
        c.setTreeFromNewick(newick);

        // Set the pseudorandom number generator
        c.setLot(_lot);

        // Provide the chain a likelihood calculator
        c.setLikelihood(_likelihood);

        // Tell the chain that it should adapt its updators (at least initially)
        c.startTuning();

        // Set heating power to precalculated value
        c.setHeatingPower(_heating_powers[chain_index++]);

        // Print headers in output files and make sure each updator has its starting value
        c.start();
        }
    }

inline void Strom::showLambdas() const
    {
    for (auto & c : _chains)
        {
        _output_manager->outputConsole(boost::str(boost::format("Chain with power %.5f") % c.getHeatingPower()));
        std::vector<std::string> names = c.getLambdaNames();
        std::vector<double> lambdas    = c.getLambdas();
        unsigned n = (unsigned)names.size();
        for (unsigned i = 0; i < n; ++i)
            {
            _output_manager->outputConsole(boost::str(boost::format("%30s %12.8f") % names[i] % lambdas[i]));
            }
        }
    }

inline void Strom::stopTuningChains()
    {
    _swaps.assign(_num_chains*_num_chains, 0);
    for (auto & c : _chains)
        {
        c.stopTuning();
        }
    }

inline void Strom::stepChains(unsigned iteration, bool sampling)
    {
    for (auto & c : _chains)
        c.nextStep(iteration, (sampling ? _sample_freq : 0));
    }

inline void Strom::swapChains()
    {
    if (_num_chains == 1)
        return;

    // Select two chains at random to swap
    // If _num_chains = 3...
    //  i  j  = (i + 1 + randint(0,1)) % _num_chains
    // ---------------------------------------------
    //  0  1  = (0 + 1 +      0      ) %     3
    //     2  = (0 + 1 +      1      ) %     3
    // ---------------------------------------------
    //  1  2  = (1 + 1 +      0      ) %     3
    //     0  = (1 + 1 +      1      ) %     3
    // ---------------------------------------------
    //  2  0  = (2 + 1 +      0      ) %     3
    //     1  = (2 + 1 +      1      ) %     3
    // ---------------------------------------------
    unsigned i = _lot->randint(0, _num_chains-1);
    unsigned j = i + 1 + _lot->randint(0, _num_chains-2);
    j %= _num_chains;

    //std::cerr << "i = " << i << std::endl;
    //std::cerr << "j = " << j << std::endl;
    //if (i == j)
    //    std::cerr << "i == j" << std::endl;
    //if (i < 0)
    //    std::cerr << "i < 0" << std::endl;
    //if (j < 0)
    //    std::cerr << "j < 0" << std::endl;
    //if (i >= _num_chains)
    //    std::cerr << "i >= _num_chains" << std::endl;
    //if (j >= _num_chains)
    //    std::cerr << "j >= _num_chains" << std::endl;

    assert(i != j && i >=0 && i < _num_chains && j >= 0 && j < _num_chains);

    // Determine upper and lower triangle cells in _swaps vector
    unsigned smaller = i;
    unsigned larger  = j;
    if (j < i)
        {
        smaller = j;
        larger  = i;
        }
    unsigned upper = smaller*_num_chains + larger;
    unsigned lower = larger*_num_chains  + smaller;
    _swaps[upper]++;

    // Propose swap of chains i and j
	// Proposed state swap will be successful if a uniform random deviate is less than R, where
	//    R = Ri * Rj = (Pi(j) / Pi(i)) * (Pj(i) / Pj(j))
    // Chain i: power = a, kernel = pi
    // Chain j: power = b, kernel = pj
    //      pj^a         pi^b
    // Ri = ----    Rj = ----
    //      pi^a         pj^b
    // log R = (a-b) [log(pj) - log(pi)]

    double heat_i       = _chains[i].getHeatingPower();
    double log_kernel_i = _chains[i].calcLogLikelihood() + _chains[i].calcLogJointPrior();

    double heat_j       = _chains[j].getHeatingPower();
    double log_kernel_j = _chains[j].calcLogLikelihood() + _chains[j].calcLogJointPrior();

    double logR = (heat_i - heat_j)*(log_kernel_j - log_kernel_i);

    double logu = _lot->logUniform();
    if (logu < logR)
        {
        // accept swap
        _swaps[lower]++;
        _chains[j].setHeatingPower(heat_i);
        _chains[i].setHeatingPower(heat_j);
        std::vector<double> lambdas_i = _chains[i].getLambdas();
        std::vector<double> lambdas_j = _chains[j].getLambdas();
        _chains[i].setLambdas(lambdas_j);
        _chains[j].setLambdas(lambdas_i);
        }

    }

inline void Strom::stopChains()
    {
    for (auto & c : _chains)
        c.stop();
    }

inline void Strom::swapSummary() const
    {
    unsigned i, j;
    std::cout << "\nSwap summary (upper triangle = no. attempted swaps; lower triangle = no. successful swaps):" << std::endl;

    // column headers
    std::cout << boost::str(boost::format("%12s") % " ");
    for (i = 0; i < _num_chains; ++i)
        std::cout << boost::str(boost::format(" %12d") % i);
    std::cout << std::endl;

    // top line
    std::cout << boost::str(boost::format("%12s") % "------------");
    for (i = 0; i < _num_chains; ++i)
        std::cout << boost::str(boost::format("-%12s") % "------------");
    std::cout << std::endl;

    // table proper
    for (i = 0; i < _num_chains; ++i)
        {
        std::cout << boost::str(boost::format("%12d") % i);
        for (j = 0; j < _num_chains; ++j)
            {
            if (i == j)
                std::cout << boost::str(boost::format(" %12s") % "---");
            else
                std::cout << boost::str(boost::format(" %12.5f") % _swaps[i*_num_chains + j]);
            }
        std::cout << std::endl;
        }

    // bottom line
    std::cout << boost::str(boost::format("%12s") % "------------");
    for (i = 0; i < _num_chains; ++i)
        std::cout << boost::str(boost::format("-%12s") % "------------");
    std::cout << std::endl;

    }


inline void Strom::run()
    {
    std::cout << "Starting..." << std::endl;

    try
        {
        // Read and store data
        Data::SharedPtr d(new Data());
        d->getDataFromFile(_data_file_name);

        // Create a substitution model
        _gtr = GTRModel::SharedPtr(new GTRModel());

        // old way
        //gtr->setExchangeabilitiesAndStateFreqs(
        //    {0.1307058, 0.1583282, 0.1598077, 0.2456609, 0.2202439, 0.08525337},
        //    {0.2249887, 0.2090694, 0.1375933, 0.4283486});
        //gtr->setGammaShape(1.480126);
        //gtr->setGammaNCateg(4);

        // new way
        _gtr->setExchangeabilitiesAndStateFreqs(_exchangeabilities, _state_frequencies);
        _gtr->setGammaShape(_gamma_shape);
        _gtr->setGammaNCateg(_num_categ);

        std::cout << _gtr->describeModel() << std::endl;

        // Create a likelihood object that will compute log-likelihoods
        // Likelihood::SharedPtr likelihood(new Likelihood());
        // likelihood->setData(d);
        // likelihood->setModel(gtr);
        _likelihood = Likelihood::SharedPtr(new Likelihood());
        _likelihood->setData(d);
        _likelihood->setModel(_gtr);

        // Read in a tree
        TreeSummary::SharedPtr tree_summary(new TreeSummary());
        tree_summary->readTreefile(_tree_file_name, 0);
        //Tree::SharedPtr tree = tree_summary->getTree(0);
        std::string newick = tree_summary->getNewick(0);
        //TreeManip::SharedPtr tm = TreeManip::SharedPtr(new TreeManip(tree));

        // Calculate the log-likelihood for the tree
        //double lnL = likelihood->calcLogLikelihood(tree);
        //std::cout << boost::str(boost::format("log likelihood = %.5f") % lnL) << std::endl;
        //if (_expected_log_likelihood != 0.0)
            std::cout << boost::str(boost::format("      (expecting %.5f)") % _expected_log_likelihood) << std::endl;

        // Create a Lot object that generates (pseudo)random numbers
        _lot = Lot::SharedPtr(new Lot);
        _lot->setSeed(_random_seed);

        // Create an output manager and open output files
        _output_manager.reset(new OutputManager);
        _output_manager->outputConsole(boost::str(boost::format("\n%12s %12s %12s") % "iteration" % "logLike" % "logPrior"));
        _output_manager->openTreeFile("trees.tre", d);
        _output_manager->openParameterFile("params.txt", _likelihood->getModel());

        //POLNEW added code
        // Create  Chain objects
        _chains.resize(_num_chains);
        _swaps.assign(_num_chains*_num_chains, 0);
        std::cout << "Number of chains = " << _num_chains << std::endl;

        //POLNEW added code
        // Create heating power vector
        _heating_powers.assign(_num_chains, 1.0);
        calcHeatingPowers();

        //POLNEW added code
        initChains(_output_manager, newick);

        //POLNEW deleted code
        // Create a Chain object and take _num_iter steps
        // Chain::SharedPtr chain = Chain::SharedPtr(new Chain);
        // chain->setLot(lot);
        // chain->setLikelihood(likelihood);
        // chain->setTreeManip(tm);
        // chain->start();
        // sample(0, chain, tm, gtr);
        // for (unsigned iteration = 1; iteration <= _num_iter; ++iteration)
        //     {
        //     chain->nextStep(iteration, _sample_freq);
        //     sample(iteration, chain, tm, gtr);
        //     }
        // chain->stop();

        //POLNEW added code
        std::cout << "Burning in for " << _num_burnin_iter << " iterations... " << std::endl;
        for (unsigned iteration = 1; iteration <= _num_burnin_iter; ++iteration)
            {
            stepChains(iteration, false);
            swapChains();
            }

        //POLNEW added code
        std::cout << "Burn-in finished, no longer tuning updaters." << std::endl;
        stopTuningChains();
        showLambdas();

        //POLNEW added code
        for (unsigned iteration = 1; iteration <= _num_iter; ++iteration)
            {
            stepChains(iteration, true);
            swapChains();
            }
        showLambdas();
        stopChains();

        //POLNEW added code
        // Create swap summary
        swapSummary();

        //POLNEW added code
        // Create tree summaries
        std::cout << "\nSummary of \"trees.tre\":" << std::endl;
        tree_summary->clear();
        tree_summary->readTreefile("trees.tre", 1);
        tree_summary->showSummary();

        // Close output files
        _output_manager->closeTreeFile();
        _output_manager->closeParameterFile();
        }
    catch (XStrom & x)  //POLWARN
        {
        std::cerr << "Strom encountered a problem:\n  " << x.what() << std::endl;
        }

    std::cout << "\nFinished!" << std::endl;
    }

}

