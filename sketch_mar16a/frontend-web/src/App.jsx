import { useEffect, useMemo, useState } from 'react'
import './App.css'

const AUTO_REFRESH_MS = 30 * 60 * 1000
const DEFAULT_API_BASE = `http://${window.location.hostname || 'localhost'}:8100`
const API_BASE = import.meta.env.VITE_API_BASE_URL || DEFAULT_API_BASE
const CHART_SIZE = {
  width: 860,
  height: 320,
  paddingTop: 20,
  paddingBottom: 34,
  paddingLeft: 46,
  paddingRight: 86,
}
const HALF_HOUR_MS = 30 * 60 * 1000

function valueToChartX(timestampMs, fromMs, toMs) {
  const total = Math.max(toMs - fromMs, 1)
  const usableWidth = CHART_SIZE.width - CHART_SIZE.paddingLeft - CHART_SIZE.paddingRight
  return CHART_SIZE.paddingLeft + ((timestampMs - fromMs) / total) * usableWidth
}

function buildPolylinePoints(series, valueKey, minValue, maxValue, fromMs, toMs) {
  if (series.length === 0) {
    return ''
  }

  const range = Math.max(maxValue - minValue, 0.1)
  const usableHeight = CHART_SIZE.height - CHART_SIZE.paddingTop - CHART_SIZE.paddingBottom

  return series
    .map((item) => {
      const value = item[valueKey]
      const x = valueToChartX(item.timestampMs, fromMs, toMs)
      const y = CHART_SIZE.paddingTop + ((maxValue - value) / range) * usableHeight
      return `${x},${y}`
    })
    .join(' ')
}

function valueToChartY(value, minValue, maxValue) {
  const range = Math.max(maxValue - minValue, 0.1)
  const usableHeight = CHART_SIZE.height - CHART_SIZE.paddingTop - CHART_SIZE.paddingBottom
  const y = CHART_SIZE.paddingTop + ((maxValue - value) / range) * usableHeight
  return Math.max(CHART_SIZE.paddingTop, Math.min(CHART_SIZE.height - CHART_SIZE.paddingBottom, y))
}

function splitSeriesByGap(series, defaultStepMs) {
  if (series.length === 0) {
    return []
  }
  if (series.length === 1) {
    return [series]
  }
  const deltas = []
  for (let i = 1; i < series.length; i += 1) {
    deltas.push(series[i].timestampMs - series[i - 1].timestampMs)
  }
  const sortedDeltas = [...deltas].sort((a, b) => a - b)
  const medianDelta = sortedDeltas[Math.floor(sortedDeltas.length / 2)] || defaultStepMs
  const gapLimit = Math.max(defaultStepMs * 1.5, medianDelta * 1.6)
  const segments = []
  let currentSegment = [series[0]]
  for (let i = 1; i < series.length; i += 1) {
    const prev = series[i - 1]
    const cur = series[i]
    if (cur.timestampMs - prev.timestampMs > gapLimit) {
      segments.push(currentSegment)
      currentSegment = [cur]
    } else {
      currentSegment.push(cur)
    }
  }
  segments.push(currentSegment)
  return segments
}

function roundTo(value, decimals = 2) {
  const factor = 10 ** decimals
  return Math.round(value * factor) / factor
}

function computeMedian(values) {
  if (values.length === 0) {
    return null
  }
  const sorted = [...values].sort((a, b) => a - b)
  const mid = Math.floor(sorted.length / 2)
  if (sorted.length % 2 === 1) {
    return sorted[mid]
  }
  return (sorted[mid - 1] + sorted[mid]) / 2
}

function computeModeRounded(values, step = 0.5) {
  if (values.length === 0) {
    return null
  }
  const counts = new Map()
  for (const value of values) {
    const rounded = roundTo(Math.round(value / step) * step, 1).toFixed(1)
    counts.set(rounded, (counts.get(rounded) || 0) + 1)
  }
  let bestKey = null
  let bestCount = -1
  for (const [key, count] of counts.entries()) {
    if (count > bestCount) {
      bestKey = key
      bestCount = count
    }
  }
  return bestKey === null ? null : Number(bestKey)
}

function computeSeriesStats(series, valueKey) {
  if (series.length === 0) {
    return null
  }
  const values = series.map((item) => item[valueKey]).filter((value) => Number.isFinite(value))
  if (values.length === 0) {
    return null
  }
  let minIndex = 0
  let maxIndex = 0
  for (let i = 1; i < series.length; i += 1) {
    if (series[i][valueKey] < series[minIndex][valueKey]) {
      minIndex = i
    }
    if (series[i][valueKey] > series[maxIndex][valueKey]) {
      maxIndex = i
    }
  }
  const mean = values.reduce((acc, value) => acc + value, 0) / values.length
  const median = computeMedian(values)
  const mode = computeModeRounded(values, 0.5)
  return {
    count: values.length,
    mean: roundTo(mean, 2),
    median: median === null ? null : roundTo(median, 2),
    mode: mode === null ? null : roundTo(mode, 1),
    min: roundTo(series[minIndex][valueKey], 2),
    max: roundTo(series[maxIndex][valueKey], 2),
    minAt: series[minIndex].timestampMs,
    maxAt: series[maxIndex].timestampMs,
    range: roundTo(series[maxIndex][valueKey] - series[minIndex][valueKey], 2),
  }
}

function App() {
  const [view, setView] = useState('current')
  const [current, setCurrent] = useState(null)
  const [history, setHistory] = useState([])
  const [floripaHistory, setFloripaHistory] = useState([])
  const [summary, setSummary] = useState(null)
  const [loading, setLoading] = useState(true)
  const [error, setError] = useState('')
  const [lastSyncAt, setLastSyncAt] = useState('')
  const [periodLabel, setPeriodLabel] = useState('')
  const [periodRange, setPeriodRange] = useState(null)

  const fetchDashboardData = async () => {
    setLoading(true)
    setError('')
    try {
      const now = new Date()
      const sevenDaysAgo = new Date(now.getTime() - 7 * 24 * 60 * 60 * 1000)
      const fromIso = sevenDaysAgo.toISOString()
      const toIso = now.toISOString()

      const periodRangeQuery = `from=${encodeURIComponent(fromIso)}&to=${encodeURIComponent(toIso)}`
      const [currentRes, historyRes, floripaRes, summaryRes] = await Promise.all([
        fetch(`${API_BASE}/api/current`),
        fetch(`${API_BASE}/api/history?${periodRangeQuery}&limit=5000`),
        fetch(`${API_BASE}/api/floripa/history?${periodRangeQuery}&limit=5000`),
        fetch(`${API_BASE}/api/stats/summary?${periodRangeQuery}&toleranceC=1&intervalMinutes=30`),
      ])

      if (!currentRes.ok || !historyRes.ok || !floripaRes.ok || !summaryRes.ok) {
        throw new Error('Falha ao buscar dados do backend')
      }

      const currentJson = await currentRes.json()
      const historyJson = await historyRes.json()
      const floripaJson = await floripaRes.json()
      const summaryJson = await summaryRes.json()

      setCurrent(currentJson.current)
      setHistory(Array.isArray(historyJson.items) ? historyJson.items : [])
      setFloripaHistory(Array.isArray(floripaJson.items) ? floripaJson.items : [])
      setSummary(summaryJson.summary || null)
      setLastSyncAt(new Date().toISOString())
      setPeriodLabel(
        `${sevenDaysAgo.toLocaleString('pt-BR', {
          day: '2-digit',
          month: '2-digit',
          hour: '2-digit',
          minute: '2-digit',
        })} até ${now.toLocaleString('pt-BR', {
          day: '2-digit',
          month: '2-digit',
          hour: '2-digit',
          minute: '2-digit',
        })}`
      )
      setPeriodRange({ fromMs: sevenDaysAgo.getTime(), toMs: now.getTime() })
    } catch {
      setError(`Não foi possível atualizar dados do backend em ${API_BASE}.`)
    } finally {
      setLoading(false)
    }
  }

  useEffect(() => {
    fetchDashboardData()
    const timer = setInterval(fetchDashboardData, AUTO_REFRESH_MS)
    return () => clearInterval(timer)
  }, [])

  const roomChartSeries = useMemo(() => {
    const sorted = [...history]
      .map((item) => ({ ...item, timestampMs: new Date(item.timestamp).getTime() }))
      .filter((item) => Number.isFinite(item.timestampMs))
      .sort((a, b) => a.timestampMs - b.timestampMs)

    const byBucket = new Map()
    for (const item of sorted) {
      const bucketMs = Math.floor(item.timestampMs / HALF_HOUR_MS) * HALF_HOUR_MS
      const existing = byBucket.get(bucketMs)
      if (existing) {
        existing.roomSum += item.tempRoomC
        existing.floripaSum += item.tempFloripaC
        existing.count += 1
      } else {
        byBucket.set(bucketMs, {
          timestampMs: bucketMs,
          roomSum: item.tempRoomC,
          floripaSum: item.tempFloripaC,
          count: 1,
        })
      }
    }

    return [...byBucket.values()]
      .map((bucket) => ({
        timestampMs: bucket.timestampMs,
        periodStart: new Date(bucket.timestampMs).toISOString(),
        avgRoomC: Math.round((bucket.roomSum / bucket.count) * 100) / 100,
        avgFloripaC: Math.round((bucket.floripaSum / bucket.count) * 100) / 100,
      }))
      .sort((a, b) => a.timestampMs - b.timestampMs)
  }, [history])

  const floripaChartSeries = useMemo(() => {
    const sorted = [...floripaHistory]
      .map((item) => ({ ...item, timestampMs: new Date(item.timestamp).getTime() }))
      .filter((item) => Number.isFinite(item.timestampMs))
      .sort((a, b) => a.timestampMs - b.timestampMs)

    const byBucket = new Map()
    for (const item of sorted) {
      const bucketMs = Math.floor(item.timestampMs / HALF_HOUR_MS) * HALF_HOUR_MS
      const existing = byBucket.get(bucketMs)
      if (existing) {
        existing.floripaSum += item.tempC
        existing.count += 1
      } else {
        byBucket.set(bucketMs, {
          timestampMs: bucketMs,
          floripaSum: item.tempC,
          count: 1,
        })
      }
    }

    return [...byBucket.values()]
      .map((bucket) => ({
        timestampMs: bucket.timestampMs,
        periodStart: new Date(bucket.timestampMs).toISOString(),
        avgFloripaC: Math.round((bucket.floripaSum / bucket.count) * 100) / 100,
      }))
      .sort((a, b) => a.timestampMs - b.timestampMs)
  }, [floripaHistory])

  const roomSegments = useMemo(
    () => splitSeriesByGap(roomChartSeries, HALF_HOUR_MS),
    [roomChartSeries]
  )

  const floripaSegments = useMemo(
    () => splitSeriesByGap(floripaChartSeries, 60 * 60 * 1000),
    [floripaChartSeries]
  )

  const fromMs = periodRange?.fromMs ?? Date.now() - 7 * 24 * 60 * 60 * 1000
  const toMs = periodRange?.toMs ?? Date.now()
  const roomValues = roomChartSeries.map((item) => item.avgRoomC)
  const floripaValues = floripaChartSeries.map((item) => item.avgFloripaC)
  const chartValues = [
    ...roomValues,
    ...floripaValues,
    current?.tempRoomC ?? null,
    current?.tempFloripaC ?? null,
  ].filter((value) => Number.isFinite(value))
  const rawMin = chartValues.length ? Math.min(...chartValues) : 0
  const rawMax = chartValues.length ? Math.max(...chartValues) : 1
  const spread = Math.max(rawMax - rawMin, 2)
  const chartMin = Math.floor((rawMin - spread * 0.15) * 2) / 2
  const chartMax = Math.ceil((rawMax + spread * 0.15) * 2) / 2
  const roomSegmentPoints = roomSegments.map((segment) =>
    buildPolylinePoints(segment, 'avgRoomC', chartMin, chartMax, fromMs, toMs)
  )
  const floripaSegmentPoints = floripaSegments.map((segment) =>
    buildPolylinePoints(segment, 'avgFloripaC', chartMin, chartMax, fromMs, toMs)
  )
  const yTicks = Array.from({ length: 5 }, (_, index) => {
    const value = chartMax - (index * (chartMax - chartMin)) / 4
    return Math.round(value * 10) / 10
  })
  const dayTicks = useMemo(() => {
    const ticks = []
    const cursor = new Date(fromMs)
    cursor.setHours(0, 0, 0, 0)
    if (cursor.getTime() < fromMs) {
      cursor.setDate(cursor.getDate() + 1)
    }
    while (cursor.getTime() <= toMs) {
      ticks.push(cursor.getTime())
      cursor.setDate(cursor.getDate() + 1)
    }
    return ticks
  }, [fromMs, toMs])
  const lastRoomChartValue = roomValues.length ? roomValues[roomValues.length - 1] : null
  const lastFloripaChartValue = floripaValues.length ? floripaValues[floripaValues.length - 1] : null
  const lastRoomBucketMs = roomChartSeries.length
    ? roomChartSeries[roomChartSeries.length - 1].timestampMs
    : null
  const lastFloripaBucketMs = floripaChartSeries.length
    ? floripaChartSeries[floripaChartSeries.length - 1].timestampMs
    : null
  const rightRoomX = lastRoomBucketMs !== null ? valueToChartX(lastRoomBucketMs, fromMs, toMs) : null
  const rightFloripaX =
    lastFloripaBucketMs !== null ? valueToChartX(lastFloripaBucketMs, fromMs, toMs) : null
  const rightRoomLabel = lastRoomChartValue
  const rightFloripaLabel = lastFloripaChartValue
  let roomLabelY = rightRoomLabel !== null ? valueToChartY(rightRoomLabel, chartMin, chartMax) : null
  let floripaLabelY =
    rightFloripaLabel !== null ? valueToChartY(rightFloripaLabel, chartMin, chartMax) : null
  if (roomLabelY !== null && floripaLabelY !== null && Math.abs(roomLabelY - floripaLabelY) < 18) {
    roomLabelY = Math.max(CHART_SIZE.paddingTop, roomLabelY - 10)
    floripaLabelY = Math.min(CHART_SIZE.height - CHART_SIZE.paddingBottom, floripaLabelY + 10)
  }
  const latestTimestamp = current?.timestamp
    ? new Date(current.timestamp).toLocaleString('pt-BR')
    : 'Sem dados'
  const lastSyncLabel = lastSyncAt
    ? new Date(lastSyncAt).toLocaleString('pt-BR')
    : 'Ainda não sincronizado'
  const roomTemp = current?.tempRoomC ?? null
  const floripaTemp = current?.tempFloripaC ?? null
  const diffValue = current?.diffC ?? null
  const diffStatus =
    diffValue === null
      ? 'Sem leitura'
      : diffValue > 0
        ? 'Quarto mais quente'
        : diffValue < 0
          ? 'Quarto mais frio'
          : 'Temperaturas iguais'
  const diffAbs = diffValue === null ? null : Math.abs(diffValue)

  let roomBarHeight = 58
  let floripaBarHeight = 58
  if (roomTemp !== null && floripaTemp !== null) {
    const absDiff = Math.abs(roomTemp - floripaTemp)
    const scale = Math.min(absDiff / 12, 1)
    const delta = scale * 30
    if (roomTemp >= floripaTemp) {
      roomBarHeight = 58 + delta / 2
      floripaBarHeight = 58 - delta / 2
    } else {
      roomBarHeight = 58 - delta / 2
      floripaBarHeight = 58 + delta / 2
    }
  }

  const roomStats = useMemo(
    () => computeSeriesStats(roomChartSeries, 'avgRoomC'),
    [roomChartSeries]
  )
  const floripaStats = useMemo(
    () => computeSeriesStats(floripaChartSeries, 'avgFloripaC'),
    [floripaChartSeries]
  )
  const comparisonSeries = useMemo(
    () =>
      roomChartSeries.map((item) => ({
        timestampMs: item.timestampMs,
        diffC: roundTo(item.avgRoomC - item.avgFloripaC, 2),
      })),
    [roomChartSeries]
  )
  const comparisonStats = useMemo(
    () => computeSeriesStats(comparisonSeries, 'diffC'),
    [comparisonSeries]
  )

  return (
    <main className="mobile-shell">
      <header className="appbar">
        <div className="app-title-wrap">
          <h1>Termômetro Floripa</h1>
          <p>Atualização automática a cada 30 minutos</p>
        </div>
        <button className="refresh-btn" onClick={fetchDashboardData}>
          Atualizar
        </button>
      </header>

      {error && <section className="error-box">{error}</section>}

      {loading ? <section className="panel">Carregando dados...</section> : null}

      {!loading && view === 'current' && (
        <>
          <section className="compare-visual">
            <div className="compare-top">
              <article className="compare-side">
                <p className="title-room">Quarto</p>
                <strong className="temp-room">{roomTemp !== null ? `${roomTemp} °C` : '--'}</strong>
              </article>
              <article className="compare-side">
                <p className="title-floripa">Floripa</p>
                <strong className="temp-floripa">
                  {floripaTemp !== null ? `${floripaTemp} °C` : '--'}
                </strong>
              </article>
            </div>

            <div className="bar-stage">
              <div className="bar-column bar-room" style={{ height: `${roomBarHeight}%` }} />
              <div className="bar-column bar-floripa" style={{ height: `${floripaBarHeight}%` }} />
            </div>

            <div className="diff-overlay">
              <p>Diferença:</p>
              <strong>{diffAbs !== null ? `${diffAbs} °C` : '--'}</strong>
              <span>{diffStatus}</span>
            </div>
          </section>

          <section className="panel">
          <h2>Resumo dos últimos 7 dias</h2>
          <p className="period-line">Período analisado: {periodLabel || 'Últimos 7 dias'}</p>
          <p className="period-line">Comparação em blocos de 30 min com horários em que a placa esteve ligada.</p>
          <p className="period-line">
            Blocos de comparação analisados: {summary ? summary.count : '--'} (cada bloco = 30 min com leitura da placa).
          </p>
          <h3 className="stats-title">Comparação (Quarto - Floripa)</h3>
          <div className="stats-list">
            <p>
              <span>Média da diferença</span>
              <strong>{summary ? `${summary.avgDiffC} °C` : '--'}</strong>
            </p>
            <p>
              <span>Mediana da diferença</span>
              <strong>{comparisonStats ? `${comparisonStats.median} °C` : '--'}</strong>
            </p>
            <p>
              <span>Moda da diferença (arred. 0,5°C)</span>
              <strong>{comparisonStats ? `${comparisonStats.mode} °C` : '--'}</strong>
            </p>
            <p>
              <span>Maior vantagem do Quarto</span>
              <strong>{comparisonStats ? `${comparisonStats.max} °C` : '--'}</strong>
            </p>
            <p>
              <span>Maior vantagem de Floripa</span>
              <strong>{comparisonStats ? `${comparisonStats.min} °C` : '--'}</strong>
            </p>
            <p>
              <span>Percentual acima de Floripa</span>
              <strong>{summary ? `${summary.aboveFloripaPct}%` : '--'}</strong>
            </p>
            <p>
              <span>Blocos dentro de ±1°C</span>
              <strong>{summary ? `${summary.withinTolerancePct}%` : '--'}</strong>
            </p>
          </div>
          <h3 className="stats-title">Quarto (blocos com placa ligada)</h3>
          <div className="stats-list">
            <p>
              <span>Média | Mediana | Moda</span>
              <strong>
                {roomStats ? `${roomStats.mean} | ${roomStats.median} | ${roomStats.mode} °C` : '--'}
              </strong>
            </p>
            <p>
              <span>Pico de temperatura</span>
              <strong>
                {roomStats ? `${roomStats.max} °C (${new Date(roomStats.maxAt).toLocaleString('pt-BR')})` : '--'}
              </strong>
            </p>
            <p>
              <span>Menor temperatura</span>
              <strong>
                {roomStats ? `${roomStats.min} °C (${new Date(roomStats.minAt).toLocaleString('pt-BR')})` : '--'}
              </strong>
            </p>
            <p>
              <span>Amplitude térmica</span>
              <strong>{roomStats ? `${roomStats.range} °C` : '--'}</strong>
            </p>
          </div>
          <h3 className="stats-title">Floripa (histórico completo no período)</h3>
          <div className="stats-list">
            <p>
              <span>Média | Mediana | Moda</span>
              <strong>
                {floripaStats
                  ? `${floripaStats.mean} | ${floripaStats.median} | ${floripaStats.mode} °C`
                  : '--'}
              </strong>
            </p>
            <p>
              <span>Pico de temperatura</span>
              <strong>
                {floripaStats
                  ? `${floripaStats.max} °C (${new Date(floripaStats.maxAt).toLocaleString('pt-BR')})`
                  : '--'}
              </strong>
            </p>
            <p>
              <span>Menor temperatura</span>
              <strong>
                {floripaStats
                  ? `${floripaStats.min} °C (${new Date(floripaStats.minAt).toLocaleString('pt-BR')})`
                  : '--'}
              </strong>
            </p>
            <p>
              <span>Amplitude térmica</span>
              <strong>{floripaStats ? `${floripaStats.range} °C` : '--'}</strong>
            </p>
          </div>
          </section>
        </>
      )}

      {!loading && view === 'chart' && (
        <section className="panel chart-panel">
          <h2>Comparação ao longo dos últimos 7 dias</h2>
          <p className="period-line">Período analisado: {periodLabel || 'Últimos 7 dias'}</p>
          {roomChartSeries.length < 2 && floripaChartSeries.length < 2 ? (
            <p className="hint">Dados insuficientes para gráfico. Gere mais leituras mock.</p>
          ) : (
            <>
              <svg viewBox={`0 0 ${CHART_SIZE.width} ${CHART_SIZE.height}`} className="chart" role="img">
                {yTicks.map((tick) => {
                  const y = valueToChartY(tick, chartMin, chartMax)
                  return (
                    <g key={`y-${tick}`} className="chart-grid-row">
                      <line
                        x1={CHART_SIZE.paddingLeft}
                        y1={y}
                        x2={CHART_SIZE.width - CHART_SIZE.paddingRight}
                        y2={y}
                      />
                      <text x={CHART_SIZE.paddingLeft - 8} y={y + 4} textAnchor="end">
                        {tick.toFixed(1)}°
                      </text>
                    </g>
                  )
                })}

                {dayTicks.map((tickMs, index) => {
                  const x = valueToChartX(tickMs, fromMs, toMs)
                  return (
                    <g key={`x-day-${index}`} className="chart-grid-col">
                      <line
                        x1={x}
                        y1={CHART_SIZE.paddingTop}
                        x2={x}
                        y2={CHART_SIZE.height - CHART_SIZE.paddingBottom}
                      />
                      <text
                        x={x}
                        y={CHART_SIZE.height - 8}
                        textAnchor="middle"
                      >
                        {new Date(tickMs).toLocaleDateString('pt-BR', {
                          day: '2-digit',
                          month: '2-digit',
                        })}
                      </text>
                    </g>
                  )
                })}

                {roomSegmentPoints.map((points, index) => (
                  <polyline key={`room-segment-${index}`} className="line-room" points={points} />
                ))}
                {floripaSegmentPoints.map((points, index) => (
                  <polyline key={`floripa-segment-${index}`} className="line-floripa" points={points} />
                ))}

                {lastRoomChartValue !== null && rightRoomX !== null && (
                  <circle
                    cx={rightRoomX}
                    cy={valueToChartY(lastRoomChartValue, chartMin, chartMax)}
                    r="4"
                    className="dot-room"
                  />
                )}
                {lastFloripaChartValue !== null && rightFloripaX !== null && (
                  <circle
                    cx={rightFloripaX}
                    cy={valueToChartY(lastFloripaChartValue, chartMin, chartMax)}
                    r="4"
                    className="dot-floripa"
                  />
                )}

                {rightRoomLabel !== null && roomLabelY !== null && rightRoomX !== null && (
                  <text x={rightRoomX + 10} y={roomLabelY + 4} className="live-label-room">
                    {rightRoomLabel}°
                  </text>
                )}
                {rightFloripaLabel !== null && floripaLabelY !== null && rightFloripaX !== null && (
                  <text x={rightFloripaX + 10} y={floripaLabelY + 4} className="live-label-floripa">
                    {rightFloripaLabel}°
                  </text>
                )}
              </svg>
              <div className="legend">
                <span className="room">Quarto</span>
                <span className="floripa">Florianópolis</span>
              </div>
            </>
          )}
        </section>
      )}

      <section className="panel info-panel">
        <p>Último dado: {latestTimestamp}</p>
        <p>Última sincronização: {lastSyncLabel}</p>
        <p className="backend">Backend: {API_BASE}</p>
      </section>

      <section className="panel info-panel">
        <p>Média: soma de todas as temperaturas dividida pela quantidade de leituras.</p>
        <p>Mediana: valor central quando as temperaturas são ordenadas.</p>
        <p>Moda: temperatura que mais se repete no período.</p>
      </section>

      <nav className="bottom-tabs">
        <button
          className={view === 'current' ? 'active' : ''}
          onClick={() => setView('current')}
        >
          Comparação
        </button>
        <button
          className={view === 'chart' ? 'active' : ''}
          onClick={() => setView('chart')}
        >
          Gráfico
        </button>
      </nav>
    </main>
  )
}

export default App
