import type { ClientModule, ClientSurface } from 'claude-code'

type Point = { x: number; y: number }
type Dir = 'up' | 'down' | 'left' | 'right'

type Game = {
  snake: Point[] // cabeça primeiro
  dir: Dir
  queued: Dir[] // setas apertadas entre um passo e outro
  food: Point
  score: number
  best: number
  manual: boolean // false: piloto automático
  dead: number // passos restantes da pausa depois de morrer
  width: number
  height: number
}

const STEP: Record<Dir, Point> = { up: { x: 0, y: -1 }, down: { x: 0, y: 1 }, left: { x: -1, y: 0 }, right: { x: 1, y: 0 } }
const OPPOSITE: Record<Dir, Dir> = { up: 'down', down: 'up', left: 'right', right: 'left' }
const WASD: Record<string, Dir> = { w: 'up', s: 'down', a: 'left', d: 'right' }

const same = (a: Point, b: Point) => a.x === b.x && a.y === b.y
const move = (p: Point, d: Dir): Point => ({ x: p.x + STEP[d].x, y: p.y + STEP[d].y })
const inside = (g: Game, p: Point) => p.x >= 0 && p.y >= 0 && p.x < g.width && p.y < g.height
const headOf = (g: Game): Point => g.snake[0] ?? { x: 0, y: 0 }
const hitsBody = (g: Game, p: Point) => g.snake.slice(0, -1).some(s => same(s, p))

function placeFood(g: Game): Point {
  for (let tries = 0; tries < 200; tries++) {
    const p = { x: Math.floor(Math.random() * g.width), y: Math.floor(Math.random() * g.height) }
    if (!g.snake.some(s => same(s, p))) return p
  }
  return { x: 0, y: 0 }
}

function newGame(width: number, height: number, best: number, manual: boolean): Game {
  const y = Math.floor(height / 2)
  const x = Math.floor(width / 3)
  const g: Game = {
    snake: [{ x, y }, { x: x - 1, y }, { x: x - 2, y }],
    dir: 'right', queued: [], food: { x: 0, y: 0 }, score: 0, best, manual, dead: 0, width, height,
  }
  g.food = placeFood(g)
  return g
}

// Piloto automático: vai na direção da comida sem bater; se não der, qualquer saída livre.
function autopilot(g: Game): Dir {
  const head = headOf(g)
  const options = (['up', 'down', 'left', 'right'] as Dir[]).filter(d => d !== OPPOSITE[g.dir])
  const safe = options.filter(d => {
    const p = move(head, d)
    return inside(g, p) && !hitsBody(g, p)
  })
  const toward = safe
    .map(d => ({ d, dist: Math.abs(move(head, d).x - g.food.x) + Math.abs(move(head, d).y - g.food.y) }))
    .sort((a, b) => a.dist - b.dist)
  return toward[0]?.d ?? g.dir
}

function tick(g: Game): Game {
  if (g.dead > 0) {
    return g.dead === 1 ? newGame(g.width, g.height, g.best, g.manual) : { ...g, dead: g.dead - 1 }
  }
  let queued = g.queued
  let dir = g.dir
  if (g.manual) {
    while (queued.length > 0) {
      const wanted = queued[0]!
      queued = queued.slice(1)
      if (wanted !== OPPOSITE[dir] && wanted !== dir) {
        dir = wanted
        break
      }
    }
  } else {
    dir = autopilot(g)
  }
  const head = move(headOf(g), dir)
  if (!inside(g, head) || hitsBody(g, head)) {
    return { ...g, dir, queued, dead: 12, best: Math.max(g.best, g.score) }
  }
  const ate = same(head, g.food)
  const snake = [head, ...(ate ? g.snake : g.snake.slice(0, -1))]
  const next = { ...g, dir, queued, snake, score: g.score + (ate ? 1 : 0) }
  if (ate) next.food = placeFood(next)
  next.best = Math.max(next.best, next.score)
  return next
}

const Snake: ClientModule<null, Game> = (_props, surface: ClientSurface<Game>) => {
  const { Box, Text } = surface.elements
  const width = Math.max(8, Math.floor(surface.columns / 2))
  const height = Math.max(3, surface.rows - 1)

  let g = surface.state
  if (!g || g.width !== width || g.height !== height) {
    g = newGame(width, height, g?.best ?? 0, g?.manual ?? false)
    if (surface.state === undefined) {
      surface.every(110, () => {
        const current = surface.state
        if (current) surface.setState(tick(current))
      })
      surface.onKey(key => {
        const current = surface.state
        if (!current) return
        const dir = (['up', 'down', 'left', 'right'] as string[]).includes(key.key)
          ? (key.key as Dir)
          : WASD[key.key.toLowerCase()]
        if (dir) {
          surface.setState({ ...current, manual: true, queued: [...current.queued, dir].slice(-3) })
        } else if (key.key === 'p') {
          surface.setState({ ...current, manual: false, queued: [] })
        }
      })
    }
    surface.setState(g)
  }

  const lines: string[] = []
  for (let y = 0; y < g.height; y++) {
    let line = ''
    for (let x = 0; x < g.width; x++) {
      const p = { x, y }
      if (same(headOf(g), p)) line += g.dead > 0 ? '✖ ' : '◆ '
      else if (g.snake.some(s => same(s, p))) line += '■ '
      else if (same(g.food, p)) line += '● '
      else line += '· '
    }
    lines.push(line)
  }

  const mode = g.manual
    ? 'você joga (setas/WASD, P = piloto automático)'
    : 'piloto automático: clique aqui (ou ctrl+x tab) e use as setas'
  const status = `${g.dead > 0 ? 'bateu! ' : ''}pontos ${g.score} · recorde ${g.best} · ${mode}`

  return (
    <Box flexDirection="column">
      {lines.map(line => (
        <Text color={g.dead > 0 ? 'error' : 'success'}>{line}</Text>
      ))}
      <Text dimColor>{status}</Text>
    </Box>
  )
}

export default Snake
