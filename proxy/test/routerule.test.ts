import { describe, expect, it } from "vitest";
import { RULE_REV, isGenuineSelfLoop, isRotation, routeEndpoints } from "../scripts/routerule";

/**
 * THE RULE THAT TURNS A CORPUS ROW INTO A CARD.
 *
 * Every fixture below is a REAL row from vradarserver/standing-data, named with
 * its callsign, because the two bugs this file exists for were both invisible in
 * the abstract and obvious in the data.
 *
 * The headline case is DLH8985 = EGTE-EGTE-EGTE. Rev 2 handled rotations by
 * returning legs[1], which guaranteed "not first-and-last" when the requirement
 * was "not equal" -- a PROXY for the property standing in for the property. On
 * that row legs[1] is also EGTE, so the rule produced the exact self-loop it
 * existed to prevent. It fixed the shape and not the property.
 *
 * Nothing about the transformation revealed that. It was found by asserting the
 * property directly on the output, which is the argument for this whole file.
 */
describe("routeEndpoints", () => {
  // ---- THE REGRESSION CASE, FIRST -------------------------------------
  it("DLH8985 (EGTE-EGTE-EGTE): rev 2 returned a self-loop here", () => {
    // Rev 2's rule, reproduced, so the test shows the difference rather than
    // asserting it. If this ever stops failing, the fixture has lost its point.
    const rev2 = (legs: string[]): [string, string] =>
      legs.length > 2 && legs[0] === legs[legs.length - 1]
        ? [legs[0] as string, legs[1] as string]
        : [legs[0] as string, legs[legs.length - 1] as string];
    const legs = ["EGTE", "EGTE", "EGTE"];
    const [o2, d2] = rev2(legs);
    expect(o2).toBe(d2); // the bug: rev 2 manufactured a self-loop

    // Rev 3 keeps it a self-loop -- but as REALITY, not as a manufactured one.
    // Every leg is EGTE, so there is no other airport to render.
    const [o3, d3] = routeEndpoints(legs);
    expect(o3).toBe("EGTE");
    expect(d3).toBe("EGTE");
    expect(isGenuineSelfLoop(legs)).toBe(true); // <- what makes it acceptable
  });

  it("picks the first leg that DIFFERS from the origin, not simply legs[1]", () => {
    // The shape DLH8985 would have had with a real second destination.
    expect(routeEndpoints(["EGTE", "EGTE", "EGLL", "EGTE"])).toEqual(["EGTE", "EGLL"]);
    expect(routeEndpoints(["EGTE", "EGTE", "EGTE", "EGPH", "EGTE"])).toEqual(["EGTE", "EGPH"]);
  });

  // ---- rotations: the rev 2 fix, still holding -------------------------
  it("AAL1208 (KDFW-KBUR-KDFW): a rotation renders its outbound leg", () => {
    expect(routeEndpoints(["KDFW", "KBUR", "KDFW"])).toEqual(["KDFW", "KBUR"]);
  });

  it("a rotation never renders a self-loop when another airport exists", () => {
    for (const legs of [
      ["KDFW", "KBOS", "KDFW"],
      ["KDFW", "KRIC", "KDFW"],
      ["EGLL", "LFPG", "EGLL"],
    ]) {
      const [o, d] = routeEndpoints(legs);
      expect(o).not.toBe(d);
    }
  });

  // ---- through service and the ordinary majority ------------------------
  it("A-B-C through service renders A-C", () => {
    expect(routeEndpoints(["VHHH", "UACC", "EBLG"])).toEqual(["VHHH", "EBLG"]);
  });

  it("two-leg routes are first and last (94.4% of the corpus)", () => {
    expect(routeEndpoints(["EGLL", "KJFK"])).toEqual(["EGLL", "KJFK"]);
    expect(routeEndpoints(["VOCI", "VOMM"])).toEqual(["VOCI", "VOMM"]);
  });

  // ---- genuine circular flights: real data, not malformed ---------------
  it("AFR49UR / CWL91 (A-A): a genuine circular flight stays a self-loop", () => {
    // CWL91 at EGYD is an RAF Cranwell training circuit; the aircraft really did
    // depart and return to the same field. Blanking it would spend the product's
    // one honest "we do not know" signal on data we DO know.
    expect(routeEndpoints(["LFMN", "LFMN"])).toEqual(["LFMN", "LFMN"]);
    expect(routeEndpoints(["EGYD", "EGYD"])).toEqual(["EGYD", "EGYD"]);
    expect(isGenuineSelfLoop(["EGYD", "EGYD"])).toBe(true);
  });

  it("isGenuineSelfLoop separates reality from a manufactured loop", () => {
    expect(isGenuineSelfLoop(["EGTE", "EGTE", "EGTE"])).toBe(true);
    // A row with a distinct airport in it must NEVER be excused as circular --
    // this is the predicate the verifier's tripwire hangs on.
    expect(isGenuineSelfLoop(["KDFW", "KBUR", "KDFW"])).toBe(false);
    expect(isGenuineSelfLoop(["EGLL", "KJFK"])).toBe(false);
  });

  // ---- the property, asserted directly over the whole shape space -------
  it("PROPERTY: output is a self-loop ONLY when the source is all-same", () => {
    const fields = ["A", "B", "C"];
    const lists: string[][] = [];
    for (const a of fields) for (const b of fields) lists.push([a, b]);
    for (const a of fields) for (const b of fields) for (const c of fields) lists.push([a, b, c]);
    for (const a of fields) for (const b of fields) for (const c of fields)
      for (const d of fields) lists.push([a, b, c, d]);

    for (const legs of lists) {
      const [o, d] = routeEndpoints(legs);
      // This is the requirement stated as a requirement. Rev 2 satisfied a proxy
      // for it and failed the requirement itself on exactly one real row.
      expect(o === d).toBe(isGenuineSelfLoop(legs));
    }
  });

  it("isRotation identifies what the rule rewrites", () => {
    expect(isRotation(["KDFW", "KBUR", "KDFW"])).toBe(true);
    expect(isRotation(["EGLL", "KJFK"])).toBe(false);   // two-leg is never a rotation
    expect(isRotation(["VHHH", "UACC", "EBLG"])).toBe(false);
  });

  it("RULE_REV is 3 -- bump it whenever the above changes", () => {
    // Pinned because the diff keys off it: a rule change with a stale rev writes
    // zero keys and reports success, which is how a fix survives its own release.
    expect(RULE_REV).toBe(3);
  });
});
