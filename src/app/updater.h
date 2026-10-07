#pragma once

#include <atomic>
#include <mutex>
#include <string>
#include <thread>

namespace app {

// Atualização automática pelos releases do GitHub. Uma vez por dia procura a versão nova; se houver,
// baixa o instalador para %LOCALAPPDATA%\DownloadManager\update e confere o SHA-256. A instalação
// (instalador silencioso) fica para quando o app estiver ocioso na bandeja, fechar ou abrir de novo.
class Updater {
public:
    enum class State { Idle, Checking, Current, Downloading, Ready, Failed };

    explicit Updater(std::wstring dataDirectory);
    ~Updater();

    // Procura em segundo plano se já passou um dia desde a última vez (force: agora).
    void checkIfDue(bool force = false);
    State state() const { return state_; }
    // Versão nova encontrada (vazio se nenhuma).
    std::wstring availableVersion() const;
    bool ready() const { return state_ == State::Ready; }

    // Abre o instalador baixado em modo silencioso; o app deve fechar logo depois.
    // relaunch: "/abrir", "/bandeja" ou nullptr (não reabrir). false se não deu (arquivo sumiu ou foi alterado).
    // silent=false: abre a janela do instalador (cópia do app que não foi instalada pelo setup).
    bool launchInstaller(const wchar_t* relaunch, bool silent = true);
    // Ao abrir: já houve uma tentativa de instalar esta versão? (evita repetir um instalador que falha)
    bool alreadyAttempted() const;

private:
    void run();
    void setFailed();

    std::wstring directory_;
    std::wstring setupPath_;
    std::thread worker_;
    std::atomic<State> state_{State::Idle};
    mutable std::mutex mutex_;
    std::string version_;
    std::string checksum_;
};

}  // namespace app
