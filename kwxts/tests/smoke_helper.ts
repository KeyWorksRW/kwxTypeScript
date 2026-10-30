// kwx_ts — Phase 5 TypeScript transpilation helper module.
//
// This file exercises multi-file TypeScript: it uses an interface, an enum, and
// a fully-typed exported function, so smoke.ts must transpile this module too
// (via the relative import) for the values below to be produced.

export interface Point {
    x: number;
    y: number;
}

export enum Shape {
    Circle = 1,
    Square = 2,
    Triangle = 3
}

export function areaOfSquare(side: number): number {
    return side * side;
}

export function describeShape(shape: Shape): string {
    switch (shape) {
        case Shape.Circle:
            return "circle";
        case Shape.Square:
            return "square";
        case Shape.Triangle:
            return "triangle";
    }
    return "unknown";
}

export function midpoint(a: Point, b: Point): Point {
    return { x: (a.x + b.x) / 2, y: (a.y + b.y) / 2 };
}
