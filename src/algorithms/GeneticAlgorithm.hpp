#pragma once

#include "../core/Algorithm.hpp"
#include "SequenceSearch.hpp"

#include <random>
#include <vector>

namespace dashback {

// Evolutionary search over hold-sequences. One genome is played per attempt;
// after each population has been evaluated, the algorithm keeps elites, crosses
// over successful sequences, and mutates around the best known frontier.
// Candidates are evaluated sequentially by the real game, not as simultaneous
// PlayerObjects. This keeps the search grounded in actual GD physics.
class GeneticAlgorithm : public Algorithm {
public:
    std::string name() const override { return "genetic"; }

    void onLevelStart(const LevelInfo& level) override;
    InputState decide(const StepContext& ctx) override;
    void onDeath(const DeathInfo& info) override;
    void onComplete(const AttemptResult& result) override;

    std::vector<bool> solution() const override { return m_best; }

private:
    void evolve();
    const seq::Genome& tournament();

    std::mt19937 m_rng{1337};
    std::vector<seq::Genome> m_pop;
    std::vector<int> m_fitness;   // physics frame reached, per genome
    seq::Genome m_best;           // best genome across all generations
    int m_bestFit = -1;
    int m_generationBest = -1;
    int m_lastGenerationBest = -1;
    int m_stagnantGenerations = 0;

    int m_current = 0;            // genome being evaluated this attempt
    int m_generation = 0;

    int m_popSize = 30;
    int m_eliteCount = 4;
    double m_mutationRate = 0.08;
    int m_mutationWindow = 80;
    double m_holdProb = 0.08;
};

} // namespace dashback
