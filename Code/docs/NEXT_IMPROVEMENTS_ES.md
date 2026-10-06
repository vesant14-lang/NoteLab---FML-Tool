# Propuestas tras cerrar la primera build pública

Ideas, no funciones anunciadas como implementadas.

## Mayor valor inmediato

1. **Proyecto portable `.fmlnote.zip`**: incluir solo recursos usados, detectar
   dependencias y permitir reconectar carpetas/cache sin editar JSON.
2. **Recetas de composición con nombres**: guardar varias combinaciones de
   notas, receptores y ranking; escoger una por strumline o canción.
3. **Asignación por arrastre**: arrastrar una animación del inspector a su
   pieza/carril, con preview del resultado antes de confirmar.
4. **Mapa de cobertura de bloques**: distinguir claramente simulado,
   solo exportado y no disponible por motor/versión; ampliar el simulador seguro.
5. **Perfiles por versión**: contratos explícitos Psych 0.6/0.7/1.x,
   Codename y V-Slice, con un proyecto de prueba ejecutable por destino.

## QoL

- Búsqueda global de estilos, piezas y animaciones; favoritos y recientes.
- Presets de importación/rangos y multi-selección de asignaciones.
- Loop A–B para probar un tipo custom o receptor en un fragmento del chart.
- Comparación antes/después y export del diff de configuración.
- Auto-recuperación de sesión, sin sobrescribir el proyecto guardado.
- Progreso/cancelación del escaneo y del packing grande; catálogo en segundo plano.
- Revisión de dependencias antes del export y cola de varios paquetes.
- Acciones/atajos de offset y alineación en el inspector.

## Ampliaciones

- Designer de HUD con posiciones exportables y contratos por motor;
  mantener separados layout real y ajustes de preview.
- Strumlines multikey configurables, respetando los datos del mod en lugar
  de limitar toda la vista a cuatro carriles.
- Sesiones con mezclas diferentes por jugador/rival sin perder los estilos fuente.
- Casos de integración automática en mods de prueba de los tres motores.

Primero portabilidad/recuperación y presets de composición. Después layout
exportable y multikey, porque requieren modelos y pruebas más amplios.
