#pragma once

#include <condition_variable>
#include <cstdint>
#include <deque>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

#include "core/rules.h"

namespace app {

// Aplica a regra a um arquivo concluído: move para a pasta da regra e, se pedido, extrai.
// Roda numa thread própria: mover para outro disco ou extrair pode demorar.
class Organizer {
public:
    struct Job {
        uint64_t id = 0;
        std::wstring path;        // arquivo concluído
        std::wstring baseFolder;  // pasta padrão (base das pastas relativas das regras)
        dm::Rule rule;
    };
    struct Result {
        uint64_t id = 0;
        std::wstring path;        // onde o arquivo (ou a pasta extraída) ficou
        bool openFile = false;
        bool openFolder = false;
    };

    Organizer();
    ~Organizer();

    void submit(Job job);
    std::vector<Result> takeResults();
    // Espera terminar o que está na fila (ao fechar o app).
    void drain();

private:
    void run();
    Result organize(const Job& job);

    std::mutex mutex_;
    std::condition_variable wake_;
    std::deque<Job> jobs_;
    std::vector<Result> results_;
    bool stopping_ = false;
    bool working_ = false;
    std::condition_variable idle_;
    std::thread worker_;
};

}  // namespace app
