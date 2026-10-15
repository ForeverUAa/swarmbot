#include "GeneticAlgorithm.hpp"

#include <Geode/Geode.hpp>

#include <algorithm>
#include <numeric>

namespace dashback {

void GeneticAlgorithm::onLevelStart(const LevelInfo& /*level*/) {
    m_popSize = std::max(4, static_cast<int>(
        geode::Mod::get()->getSettingValue<int64_t>("population-size")));
    m_mutationRate = geode::Mod::get()->getSettingValue<double>("mutation-rate");
    m_mutationWindow = std::max(1, static_cast<int>(
        geode::Mod::get()->getSettingValue<int64_t>("mutation-window")));
    m_eliteCount = std::clamp(m_popSize / 6, 1, m_popSize - 1);

    m_rng.seed(1337);
    m_pop.assign(m_popSize, seq::Genome{});
    m_fitness.assign(m_popSize, 0);
    m_best.clear();
    m_bestFit = -1;
    m_generationBest = -1;
    m_lastGenerationBest = -1;
    m_stagnantGenerations = 0;
    m_current = 0;
    m_generation = 0;
}

InputState GeneticAlgorithm::decide(const StepContext& ctx) {
    auto& g = m_pop[m_current];
    int f = ctx.frame;
    if (f >= static_cast<int>(g.size())) {
        std::bernoulli_distribution hold(m_holdProb);
        while (static_cast<int>(g.size()) <= f) g.push_back(hold(m_rng));
    }
    return {g[static_cast<std::size_t>(f)]};
}

void GeneticAlgorithm::onDeath(const DeathInfo& info) {
    m_fitness[m_current] = info.frame;
    m_generationBest = std::max(m_generationBest, info.frame);
    if (info.frame > m_bestFit) {
        m_bestFit = info.frame;
        m_best = m_pop[m_current];
    }
    if (++m_current >= m_popSize) evolve();
}

void GeneticAlgorithm::onComplete(const AttemptResult& /*result*/) {
    m_best = m_pop[m_current]; // the genome that finished the level
}

const seq::Genome& GeneticAlgorithm::tournament() {
    std::uniform_int_distribution<int> pick(0, m_popSize - 1);
    int a = pick(m_rng), b = pick(m_rng);
    return (m_fitness[a] >= m_fitness[b]) ? m_pop[a] : m_pop[b];
}

void GeneticAlgorithm::evolve() {
    if (m_generationBest > m_lastGenerationBest) {
        m_stagnantGenerations = 0;
        m_lastGenerationBest = m_generationBest;
    } else {
        ++m_stagnantGenerations;
    }

    std::vector<int> order(m_popSize);
    std::iota(order.begin(), order.end(), 0);
    std::sort(order.begin(), order.end(),
        [&](int a, int b) { return m_fitness[a] > m_fitness[b]; });

    std::vector<seq::Genome> next;
    next.reserve(m_popSize);

    // Elitism guarantees that the best candidates survive unchanged.
    for (int e = 0; e < m_eliteCount; ++e) {
        next.push_back(m_pop[order[e]]);
    }

    const bool injectRandomCandidates = m_stagnantGenerations >= 4;
    const double mutationRate = std::min(
        1.0, m_mutationRate * (1.0 + static_cast<double>(m_stagnantGenerations) / 3.0));

    while (static_cast<int>(next.size()) < m_popSize) {
        const auto childIndex = static_cast<int>(next.size());

        // If the population is stuck, reserve some slots for fresh candidates.
        // Empty genomes generate new random inputs as they advance through a run.
        if (injectRandomCandidates && childIndex % 4 == 0) {
            next.emplace_back();
            continue;
        }

        seq::Genome child = seq::crossover(tournament(), tournament(), m_rng);
        seq::mutateAround(
            child, m_bestFit, m_mutationWindow, mutationRate, m_holdProb, m_rng);
        next.push_back(std::move(child));
    }

    m_pop = std::move(next);
    m_fitness.assign(m_popSize, 0);
    m_current = 0;
    m_generationBest = -1;
    ++m_generation;

    geode::log::info(
        "dashback[genetic]: generation {} done, frontier frame {}, stagnant generations {}{}",
        m_generation, m_bestFit, m_stagnantGenerations,
        injectRandomCandidates ? " (random candidates injected)" : "");
}

} // namespace dashback
