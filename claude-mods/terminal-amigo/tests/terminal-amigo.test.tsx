import { expect, test } from 'claude-code/testing'

const band = (isWorking: boolean) => ({
  component: 'AbovePrompt' as const,
  props: { hasSurvey: false, isWorking, maxRows: 12, bodyColumns: 80, scroll: { offset: 0, bodyRows: 12 }, view: {} },
})

const drawn = async (ui: { drawn: (s?: { in?: string }) => Promise<unknown> }) =>
  JSON.stringify(await ui.drawn({ in: 'cobrinha' }))

test('parado mostra os botões e um botão manda o pedido', async ($, on) => {
  const sent: string[] = []
  on('prompt.submit', (_$, e) => {
    sent.push(e.text)
    return { text: e.text }
  })
  for (const surface of ['terminal', 'desktop'] as const) {
    const ui = await $.ui.mount({ plugin: 'terminal-amigo', surface, ...band(false) })
    expect(await ui.findAll({ type: 'Button' })).toHaveLength(5)
    expect(await ui.find({ type: 'Client' })).toBeUndefined()
    await ui.press({ key: 'novidades' })
    await ui.unmount()
  }
  expect(sent.length).toBe(2)
  expect(sent[0]).toContain('git pull')
})

test('trabalhando mostra a cobrinha, que anda sozinha e obedece às setas', async $ => {
  for (const surface of ['terminal', 'desktop'] as const) {
    const ui = await $.ui.mount({ plugin: 'terminal-amigo', surface, ...band(true) })
    expect(await ui.find({ type: 'Button' })).toBeUndefined()
    await ui.resize({ columns: 40, rows: 9, in: 'cobrinha' })
    const first = await drawn(ui)
    expect(first).toContain('piloto automático')
    await ui.advance(500)
    expect(await drawn(ui)).not.toBe(first)
    await ui.key({ key: 'up', in: 'cobrinha' })
    await ui.advance(120)
    expect(await drawn(ui)).toContain('você joga')
    await ui.unmount()
  }
})
