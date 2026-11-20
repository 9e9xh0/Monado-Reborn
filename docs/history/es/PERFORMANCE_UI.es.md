# Rendimiento y nueva interfaz — T259–T261

> **Archived:** Spanish original kept for context; not maintained. The maintained English version is
> [PERFORMANCE_UI.md](../../PERFORMANCE_UI.md).

Esta continuación añade optimizaciones internas, perfiles compartidos y una interfaz grafito/verde. No cambia los archivos originales del juego. La evidencia de esta máquina se registra en `docs/evidence/2026-11-04-performance-ui/summary.md`; no equivale a certificar otras GPU ni todo el juego.

## Uso

Ejecutar `Play Monado.bat`. En **Inicio**, elegir por separado calidad y objetivo de FPS. Para el Ryzen 7 7730U, el punto de partida propuesto es **Patata + 30 FPS**. Es un objetivo, no una garantía de rendimiento.

| Perfil | Resolución interna dinámica | Ajustes aplicados |
|---|---|---|
| Equilibrado | Sin límite absoluto adicional; coincide con la ventana | FSR automático, AA adicional desactivado, desenfoque original, efectos originales |
| Rendimiento | 540–720 píxeles de altura | FSR 1, sin SMAA, DOF ni desenfoque; generación de cuadros desactivada; cachés reducidas |
| Patata | 360–540 píxeles de altura | Los mismos recortes seguros que Rendimiento, con menor resolución |
| Personalizado | Manual | Conserva los cambios individuales y sus límites |

Los rangos conservan el aspecto y no superan la salida. La resolución de menús/HUD del propio juego también puede perder nitidez. Cambiar los FPS, el desenfoque del juego o la política de recursos requiere reiniciar para aplicar el perfil completo. Cambiar calidad no cambia los FPS solicitados, GPU, volumen, controles ni guardados.

Oclusión ambiental, reflejos, bloom, rayos de luz y sombras de nubes son opciones Original / Desactivado / Activado del launcher y de F1 (`runtime/graphics_options.h`), aplicadas en vivo sobre la copia por vista de la configuración de render (sin editar `lib_nx.ini`): verificado con capturas deterministas y `XCDE_GRAPHICS_LIVE=<frame>:<clave>=<valor>,...` (2026-11-17; bloom Desactivado: ~17 pases de render menos por frame). Las antiguas casillas experimentales (`effects_off`) se convierten al cargar en Desactivado.

## Optimización interna

- Descriptores Vulkan: caché acotada a 64 entradas, identidad completa de layout/bindings/buffers/rangos/vistas/samplers y secuencia de envío. Conserva preparación, subidas y transiciones incluso en un acierto. Evita asignación/actualización y enlaces idénticos; se invalida al cambiar command buffer, estado externo o reiniciar.
- Índices: scratch CPU reutilizable al expandir quads/fans, sin `malloc/free` por dibujo ni lectura de memoria GPU write-combined; inicializa el padding copiado.
- Texturas: índice por dirección, manteniendo los mismos criterios de compatibilidad, orden de precedencia y transferencia del propietario. La destrucción GPU sigue diferida.
- RomFS: caché de 256 descriptores con cuatro vías y reemplazo de la entrada libre menos reciente. Las referencias de lectores activos siguen protegidas por mutex; cuando están ocupadas todas las vías se usa un descriptor privado.
- Propiedades inmutables GPU: se consultan al seleccionar el dispositivo. El menú consulta presupuestos como máximo cada 500 ms.
- Presentación: las repeticiones usan solamente el presupuesto restante del cuadro nativo. No añaden un refresco cuando el cuadro ya llega tarde.
- Resolución dinámica: temporización GPU asíncrona, suma de comandos principales y subidas, manejo del wrap de timestamps y antigüedad de muestras. Las compilaciones/cargas no deben reducir calidad. Los timestamps se escriben con resolución dinámica o perfilado, no en el perfil Equilibrado normal.
- Compilación: el presupuesto predeterminado deja de omitir dibujos pendientes. Una variante no precargada puede provocar un tirón, pero no desaparecer temporalmente para cumplir un presupuesto arbitrario.

No se han reescrito el direccionamiento global por ventanas, el traductor de control de flujo, el modelo de colas ni el renderer mediante bindless/múltiples hilos. Son trabajos posteriores con medición y aceptación independientes. El staging completo sigue siendo una alternativa de compatibilidad costosa; no se presenta como una optimización.

## Configuración y diagnóstico

Se conservan `gpu_preset = balanced|performance`; se añaden `potato|custom`. Nuevas claves: `dynamic_min_height`, `dynamic_max_height` (0 conserva el comportamiento relativo a ventana), `effects_off` (máscara de cinco efectos; desde 2026-11-17 se convierte al cargar en la opción Desactivado de `ambient_occlusion`, `reflections`, `bloom`, `godrays` y `cloud_shadows`, y ya no tiene casillas propias) y `low_memory`. Cargar un INI antiguo conserva sus ajustes individuales: aplicar un perfil es una acción explícita. Launcher y menú usan `runtime/quality_policy.h`.

`Play Monado (performance metrics).bat` escribe `build/native_metrics.csv`. Activa perfilado detallado, así que las comparaciones deben usar el mismo modo. Registra FPS nativos/intervalos, tiempo GPU, CPU/esperas/presentación, llamadas de descriptores y bytes de staging; las categorías de tiempo pueden solaparse y no deben sumarse. El escritor vacía periódicamente el buffer y en las capturas planificadas; un cierre forzado puede perder su último tramo.

```powershell
.venv/Scripts/python -X utf8 tools/performance_report.py build/native_metrics.csv --start-frame 700 --output build/performance.json
```

Para una prueba con menos instrumentación, `XCDE_NATIVE_INTERVALS=<csv>` registra solamente intervalos nativos y una identidad de GPU/driver/configuración. `tools/benchmark_native.py` alterna referencia/candidato, usa guardados separados y no reemplaza evidencia existente. Ambos ejecutables deben incorporar ese observador; el de referencia local contiene solamente ese añadido de medida sobre las fuentes originales.

```powershell
.venv/Scripts/python -X utf8 tools/benchmark_native.py --reference build/xcde_reference.exe --candidate build/xcde_optimized.exe --ini build/qa/balanced.ini --present --output build/ab-new
```

Overrides de diagnóstico: `XCDE_DESCRIPTOR_CACHE=0` desactiva reutilización de sets; `XCDE_GPU_TIMING=0` desactiva consultas GPU; `XCDE_REPEAT_PRESENT=0` desactiva las repeticiones; `XCDE_PIPE_WAIT_MS=<n>` restaura explícitamente el antiguo presupuesto que puede omitir dibujos. No son recomendaciones generales para jugar.

## Interfaz y compatibilidad

Launcher y F1 comparten colores, radios y controles de perfiles. Inicio reúne GPU y ajustes principales; GPU/Vulkan y datos técnicos quedan en paneles avanzados/Diagnóstico. Se mantienen español/inglés, teclado/mando, cambios pendientes, restauración gráfica y las opciones existentes. A anchuras efectivas pequeñas las etiquetas se colocan sobre los combos; los paneles tienen scroll para texto grande. El launcher reduce actividad cuando está inactivo y se despierta con eventos.

La ruta de construcción predeterminada continúa siendo `Build Monado.bat` con Zig/MSYS2. En esta sesión se utilizó Clang de MSYS2 instalado dentro de `build`, porque Zig falló gestionando archivos/temporales. El generador y los objetos del juego permanecen en carpetas excluidas. Recompilar el runtime completo tras este cambio de `Settings`/`VkG`; no mezclar objetos de la versión anterior con los nuevos.

AMD Vega/RDNA, Intel, dedicadas y laptops híbridas requieren aceptación por dispositivo/controlador. Las comprobaciones Vulkan de 16×16 no verifican shaders del juego. Combate, Future Connected, velocidad temporal en diferentes FPS y sesiones prolongadas siguen siendo pruebas de aceptación separadas.

La siguiente mejora debe decidirse con rutas repetibles de campo y combate: comparar GPU, CPU/esperas, memoria y p95/p99 antes de ampliar el renderer. La evidencia local incluye el orden de comprobaciones recomendado. Para la interfaz, conviene conservar Inicio con pocas decisiones y dejar GPU/Vulkan y recortes experimentales en opciones avanzadas; añadir más controles visibles o animaciones no ayuda a este objetivo.
