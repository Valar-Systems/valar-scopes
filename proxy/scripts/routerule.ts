/**
 * How a corpus row becomes a stored route value. PURE, NO IMPORTS.
 *
 * ============================================================================
 * WHY THIS IS ITS OWN MODULE
 *
 * Both the writer (ingest-routes.ts) and the verifier (verify-routes.ts) need
 * this. If each had its own copy they would be free to disagree, and then the
 * verifier would report failures that are really a rule mismatch between two
 * files -- a check that generates false alarms gets muted, and a muted check is
 * worse than none. One definition, imported twice.
 *
 * The same reasoning already applies to the CSV splitter those two files share.
 */

/**
 * VERSION OF THE VALUE-DERIVATION RULES. Bump on ANY change to how a corpus row
 * becomes a stored value.
 *
 * It is folded into the shard hash the diff uses, and that is the whole point.
 * The diff decides what to rewrite by hashing the shard's raw bytes, which
 * correctly answers "did the source change" and says nothing at all about "did
 * OUR INTERPRETATION of the source change" -- and the second is what a code
 * change causes.
 *
 * Concretely: the rev 1 -> 2 rotation fix alters 9,249 stored values without
 * touching one source byte. Under a raw-bytes-only hash every shard reads
 * unchanged, the run writes zero keys, prints a cheerful
 * `diff: 0/1575 shards changed`, and the bug survives its own fix. That is the
 * IGO7J shape -- a repair reporting success while repairing nothing -- and
 * without this constant it would recur on every future rule change.
 *
 * A rule change therefore costs a full rewrite. That is the correct price; the
 * alternative is a fix nobody can be sure ran.
 *
 *   1  first leg -> last leg for every multi-leg route (the parser we inherited)
 *   2  rotations (first == last) render the OUTBOUND leg, not a self-loop
 *   3  the outbound leg is the first leg that DIFFERS from the origin; an
 *      all-same row (A-A-A) is a genuine self-loop and is stored as one
 */
export const RULE_REV = 3;

/**
 * Pick the origin and destination to show for a route's leg list.
 *
 * TWO-LEG (94.4% of the corpus): trivially first and last.
 *
 * MULTI-LEG (5.6%) splits into two cases, and conflating them was a real bug:
 *
 *   THROUGH SERVICE   A-B-C   ->  A-C
 *       A defensible reading -- the aircraft's day starts at A and ends at C --
 *       and adsbdb largely reports the same for these.
 *
 *   ROTATION          A-B-A   ->  A-B   (the OUTBOUND leg)
 *       first == last, so "first to last" renders DFW-DFW: a card telling the
 *       customer an aircraft flew from Dallas to Dallas. Information-free, and
 *       it looks like a bug because it is one. 9,249 routes (1.49% of the
 *       corpus) are shaped this way, and measured against live traffic over US
 *       tiles it was 10.8% of resolved callsigns -- hub rotations are exactly
 *       what flies over customers.
 *
 * ============================================================================
 * THE LIMITATION, STATED RATHER THAN PAPERED OVER.
 *
 * For a rotation we do not know which leg the aircraft is currently flying --
 * DFW->BUR or BUR->DFW. That is genuinely unknowable from the schedule alone; it
 * needs the live position, which this build step does not have.
 *
 * So this does NOT try to be clever. The outbound leg is the deterministic,
 * honest choice: it is always one of the two legs actually flown, it is right
 * half the time by construction, and it never renders a self-loop. Guessing the
 * current leg would be wrong just as often while looking authoritative, which is
 * worse than being plainly approximate.
 */
export function routeEndpoints(legs: string[]): [string, string] {
  const first = legs[0] as string;
  const last = legs[legs.length - 1] as string;
  if (legs.length > 2 && first === last) {
    // REV 3: the first leg that DIFFERS from the origin, not simply legs[1].
    //
    // Rev 2 guaranteed "not first-and-last" when the requirement was "not equal"
    // -- a proxy for the property standing in for the property. DLH8985 is
    // EGTE-EGTE-EGTE, so legs[1] is ALSO EGTE and the rule produced the very
    // self-loop it existed to prevent. It fixed the shape and not the property.
    //
    // Found by the verifier's o != d assertion on the production load, which is
    // the whole argument for asserting the property directly rather than trusting
    // the transformation that was supposed to establish it.
    for (let i = 1; i < legs.length - 1; i++) {
      if (legs[i] !== first) return [first, legs[i] as string];
    }
    // Every leg is the same field: this is not a rotation with a missing leg, it
    // is a genuine circular flight. Stored as one -- see isGenuineSelfLoop.
    return [first, first];
  }
  return [first, last];
}

/**
 * True when the SOURCE row is genuinely circular -- every leg the same field.
 *
 * These are real and there are 38 of them in the corpus: training circuits,
 * scenic flights, test and positioning flights. CWL91 is EGYD-EGYD, an RAF
 * Cranwell training circuit; CLX789 is a Cargolux test circuit at Schiphol.
 * "Departed and returned to Nice" is exactly what happened, so the data is not
 * malformed and must not be blanked -- blank is this product's promised signal
 * for "we do not know", and spending it on "we know, but it looks odd" would
 * corrupt the one honest signal the card has.
 *
 * The verifier uses this to keep its o != d tripwire sharp: a self-loop that
 * traces to an all-same source row is reality and passes; a self-loop the RULE
 * manufactured from a row containing distinct airports is a bug and fails. That
 * distinction is what would still have caught DLH8985.
 *
 * The card renders these as a local flight with the field named once
 * ("Local flight - Nice") rather than NCE -> NCE, which reads as a glitch to
 * anyone who does not know what a test circuit is.
 */
export function isGenuineSelfLoop(legs: string[]): boolean {
  return legs.length >= 2 && legs.every((l) => l === legs[0]);
}

/** True when this leg list is a rotation the rule above rewrites. */
export function isRotation(legs: string[]): boolean {
  return legs.length > 2 && legs[0] === legs[legs.length - 1];
}
