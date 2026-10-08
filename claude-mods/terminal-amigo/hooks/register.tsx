import type { Register } from 'claude-code'

// Faixa acima do prompt:
// - parado: botões com os pedidos mais comuns deste projeto (clique, ou o número com a caixa vazia);
// - trabalhando: jogo da cobrinha (piloto automático; clique nele e use as setas).
const ACTIONS = [
  { key: 'novidades', label: 'Puxar novidades', text: 'Puxe as últimas mudanças do GitHub (git pull) e me diga em poucas palavras o que mudou.' },
  { key: 'testar', label: 'Compilar e abrir o app', text: 'Compile o Download Manager aqui no meu Windows e abra o app para eu testar.' },
  { key: 'enviar', label: 'Enviar pro GitHub', text: 'Faça commit e push das mudanças que fizemos, com uma mensagem em português, e me diga se o CI passou.' },
  { key: 'publicar', label: 'Publicar versão nova', text: 'Publique uma versão nova do Download Manager: suba o número da versão, dispare o release e me avise quando sair.' },
  { key: 'ajuda', label: 'O que dá pra fazer?', text: 'Me explique em poucas linhas, sem termos técnicos, o que posso pedir para você neste projeto.' },
] as const

export const register: Register = on => {
  on('ui.render', { component: 'AbovePrompt' }, async ($, e, next) => {
    if (e.props.hasSurvey) return next(e)
    if (e.surface !== 'terminal' && e.surface !== 'desktop') return next(e)
    const { Box, Button, Client, Text } = $.ui.resolve(e)

    if (e.props.isWorking) {
      const rows = Math.max(5, Math.min(10, e.props.maxRows - 1))
      return (
        <Box flexDirection="column" width={e.props.bodyColumns}>
          <Client key="cobrinha" module="./snake.tsx" width={e.props.bodyColumns} height={rows} />
        </Box>
      )
    }

    return (
      <Box flexDirection="column" width={e.props.bodyColumns}>
        <Box flexDirection="row" flexWrap="wrap" gap={1}>
          {ACTIONS.map((action, index) => (
            <Button
              key={action.key}
              label={action.label}
              hotkey={String(index + 1)}
              onPress={() => void $.prompt.submit({ text: action.text })}
            />
          ))}
        </Box>
        <Text dimColor>Ou escreva o que quiser na caixa abaixo, como no app. Esc cancela o que eu estiver fazendo.</Text>
      </Box>
    )
  })
}
