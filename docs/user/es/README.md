# Monado-Reborn — guía del usuario (español)

> Esta es la guía en español. El punto de entrada del proyecto (en inglés) es [`README.md`](../../../README.md).

## ¿Qué es Monado?

**Monado** es una **recompilación estática** de *Xenoblade Chronicles: Definitive Edition* (Nintendo Switch, 2020)
convertida en una aplicación nativa con renderizador Vulkan.

- **No es un emulador:** el código ARM64 original del juego se traduce con antelación (Ahead-of-Time) a C y se
  compila directamente para tu PC. El sistema operativo de la consola y la API gráfica (NVN) se reimplementan
  sobre el sistema anfitrión y Vulkan.
- **Requisitos:** necesitas una copia propia y legal del juego (versión 2.0.0, juego base con la actualización
  aplicada) extraída en carpetas `exefs` y `romfs`. No se incluyen archivos del juego ni formatos empaquetados
  (`.nsp`, `.xci`, `.nca`).

## Qué aporta esta versión

Este repositorio (`Monado-Reborn`) parte del proyecto Monado original (TheMonadoGuy, continuado por
`vinitaison/Monado`) y del marco de recompilación LASRecomp, y añade una experiencia más accesible, optimizada y
fácil de usar:

1. **Versión portable y compilación facilitada:** scripts y distribuciones que simplifican la configuración y
   permiten versiones portables listas para usar.
2. **Correcciones y mejoras de rendimiento:** parches orientados a la estabilidad, a los bajones de FPS y a los
   crashes (gestión de shaders pesados y compatibilidad con hardware AMD e Intel).
3. **Scripts y diagnóstico:** archivos batch para ejecutar y analizar (`Play Monado.bat`,
   `Play Monado (performance metrics).bat`, `Play Monado (stutter diagnostics).bat`, etc.) y gestión de la
   precarga de shaders para reducir el *stuttering*.

Consulta todas las actualizaciones en la página de
[Releases](https://github.com/Ivanprestef2/Monado-Reborn/releases).

## Instrucciones rápidas

1. Coloca tus carpetas `exefs` y `romfs` extraídas (versión 2.0.0) dentro de `targets/`, de modo que existan
   `targets/exefs` y `targets/romfs`. Los nombres de carpeta son en minúsculas en todos los sistemas.
2. **Versión portable:** ejecuta `Play Monado.bat`. **Compilar desde el código:** ejecuta `Build Monado.bat`
   (Windows; instala las herramientas necesarias, varios GB) o sigue [`docs/guides/BUILD-CMAKE.md`](../../guides/BUILD-CMAKE.md)
   / [`docs/guides/LINUX.md`](../../guides/LINUX.md).
3. Inicia el juego con `Play Monado.bat`: abre el lanzador (gráficos, escalado, controles, idioma) y después el
   juego. La primera vez compila los shaders y puede tardar; las siguientes usa la caché.
4. Desde otro lanzador o acceso directo: `build/xcde.exe --game <carpeta con exefs/ y romfs/>`; la forma anterior
   `build/xcde.exe <carpeta>` sigue funcionando.

Lanzadores adicionales: `Play Monado (performance metrics).bat` y `Play Monado (stutter diagnostics).bat` son
variantes de diagnóstico; `Crear Monado Portable.bat` crea una carpeta portable para otro PC o USB (solo para tu
uso personal) y `Limpiar Monado.bat` borra la caché de shaders, los reportes de crash y los logs de este PC antes
de mover la carpeta.

## Problemas corregidos

- **Optimización de shaders pesados:** mitigación progresiva de tirones de rendimiento y crashes vinculados a
  ciertos shaders problemáticos.
- **Compatibilidad ampliada:** ajustes para mejorar la estabilidad general en configuraciones variadas (gráficas
  integradas y componentes AMD/Intel).
- **Versión portable sin dependencias obligatorias:** estructura de directorios y scripts `.bat` que permiten una
  ejecución directa sin instalar herramientas de desarrollo adicionales.
- **Diagnóstico de errores:** scripts dedicados al registro de métricas de rendimiento y a la identificación de
  bloqueos por *stuttering*.

## Créditos

- **Proyecto original:** TheMonadoGuy / `vinitaison` (Monado).
- **Marco de recompilación:** LASRecomp, de Peiikai (MIT).
- **Monado II** ([brendonbusker](https://github.com/brendonbusker/Monado-II)) — mejoras de calidad de vida y cheats.
- **Desarrollo de esta versión:** Ivanprestef2, Marval53, Cha1N1, 9e9xh0.

Si encuentras un error, abre un *issue* en el repositorio.
